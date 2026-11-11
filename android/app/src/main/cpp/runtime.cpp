// Runtime inicial do BO1 (Android ARM64).
// So existe quando o C++ recompilado esta presente (BO1_HAVE_GENERATED).
//
// Historico de correcoes:
//   2026-11-08a: si_addr -> fault_addr (colisao com macro do kernel)
//   2026-11-08b: MapRegions cobre 0x0..0x100000000 (arena inteira)
//   2026-11-08c: 6 sinais, LogRaw, beat a cada 5s, kPhysEnd 640MB,
//                implementacao de XGetVideoMode, ExCreateThread, etc.
//   2026-11-08d: std::set_terminate, beat.detach(), handlers de
//                terminate/unexpected, catch(...) no RunGuest.
//   2026-11-08e: removido std::set_unexpected (C++17+). TerminateHandler
//                loga via LogRaw antes de abort().
//   2026-11-08f: bo1_LogRaw() exportada com extern "C" FORA do namespace
//                anonimo (chamada pelos .cpp gerados via patch_instrument.py),
//                si_code traduzido, ring buffer de imports, set_new_handler,
//                Log do KeBugCheckEx / RtlRaiseException.
//   2026-11-08g: backtrace() do sistema NAO linka em API 29 (so existe a
//                partir da API 33). Substituido por _Unwind_Backtrace.
//   2026-11-08h (esta):
//     - Adicionado stub blr (0x4E800020) em g_blr_stub_addr.
//     - __imp__XexGetProcedureAddress agora retorna g_blr_stub_addr em
//       *r5 (em vez de 0) e o status 0xC0000263 continua sendo retornado.
//       ATENCAO: isso e um experimento sugerido pelo Claude. A analise
//       anterior (Parte 13 do prompt de continuidade) REFUTOU a hipotese
//       do stub blr, porque sub_824CEA10 verifica se os ponteiros sao 0
//       e faz cleanup. Com o stub blr, o jogo vai usar os ponteiros como
//       se fossem funcoes reais, mas o blr nao faz nada — o jogo pode
//       seguir com estado invalido e crashar em outro lugar.
//     - Adicionado log de confirmacao no bo1_LogRaw: as primeiras 20
//       chamadas aparecem como "[bo1_LogRaw] chamada #N". Se nao aparecer
//       nenhuma, os hooks NAO estao sendo chamados (problema no patch ou
//       no link).

#ifdef BO1_HAVE_GENERATED

#include <android/log.h>
#include <jni.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <ucontext.h>
#include <unistd.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unwind.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "ppc_config.h"
#include "ppc_context.h"
#include <image.h>

struct BootStop { std::string reason; };

namespace {

uint8_t* g_base = nullptr;

std::atomic<bool> g_started{false};
std::atomic<bool> g_stop{false};
std::atomic<int>  g_state{0};
std::atomic<bool> g_finished{false};
std::atomic<uint64_t> g_totalCalls{0};
std::atomic<uint64_t> g_lastCallMs{0};

std::mutex g_logMutex;
FILE* g_logFile = nullptr;
int   g_logFd   = -1;

std::mutex g_stateMutex;
std::string g_lastImport;
std::string g_endReason;

// Ring buffer dos ultimos imports (para o log de abort)
constexpr int kLastImportsCap = 32;
std::mutex g_lastImportsMutex;
std::string g_lastImports[kLastImportsCap];
int g_lastImportsHead = 0;
int g_lastImportsCount = 0;

std::mutex g_callsMutex;
std::unordered_map<std::string,uint64_t> g_callCounts;

thread_local sigjmp_buf* t_jump = nullptr;

struct FaultInfo {
    uint64_t fault_addr    = 0;
    uint64_t fault_address = 0;
    uint64_t pc            = 0;
    uint64_t pc_so_offset  = 0;
    char     so_name[256]  = {0};
    char     so_sym[256]   = {0};
    uint64_t r1            = 0;
    uint64_t r2            = 0;
    uint64_t r13           = 0;
    uint64_t lr            = 0;
    uint64_t sp            = 0;
    int      signo         = 0;
    int      si_code       = 0;
    void*    bt[64]        = {0};
    int      bt_size       = 0;
};
thread_local FaultInfo t_fault{};

constexpr uint64_t kArenaSize  = 0x100000000ull;
constexpr uint64_t kTableStart = PPC_IMAGE_BASE + PPC_IMAGE_SIZE;
constexpr uint64_t kTableSize  = PPC_CODE_SIZE * 2;

constexpr uint32_t kHeapStart = 0x40000000u;
constexpr uint32_t kHeapEnd   = 0x80000000u;
constexpr uint32_t kPhysStart = 0xA0000000u;
constexpr uint32_t kPhysEnd   = 0xC8000000u;

constexpr uint64_t kImageGlobalsEnd = 0xA0000000ull;

struct Bump { uint32_t current; uint32_t end; };
std::mutex g_memMutex;
Bump g_heap{kHeapStart, kHeapEnd};
Bump g_phys{kPhysStart, kPhysEnd};

// Stub blr: usado por __imp__XexGetProcedureAddress quando o ordinal
// nao existe. 0x4E800020 = "blr" (branch to link register).
uint32_t g_blr_stub_addr = 0;

// ---- logging ----

void Log(const char* fmt, ...) {
    char text[2048];
    va_list args; va_start(args, fmt); vsnprintf(text, sizeof(text), fmt, args); va_end(args);
    __android_log_print(ANDROID_LOG_INFO, "BO1", "%s", text);
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (g_logFile) { fputs(text, g_logFile); fputc('\n', g_logFile); fflush(g_logFile); }
}

void LogRaw(const char* fmt, ...) {
    char text[2048];
    va_list args; va_start(args, fmt); vsnprintf(text, sizeof(text), fmt, args); va_end(args);
    __android_log_print(ANDROID_LOG_INFO, "BO1", "%s", text);
    if (g_logFd < 0) return;
    size_t n = strnlen(text, sizeof(text) - 1);
    if (n >= sizeof(text)) n = sizeof(text) - 1;
    char line[2100];
    size_t total = (n < sizeof(line) - 2) ? n : sizeof(line) - 2;
    memcpy(line, text, total);
    line[total] = '\n';
    line[total + 1] = 0;
    ssize_t ignored = write(g_logFd, line, total + 1);
    (void)ignored;
}

// Registra um import no ring buffer (chamado de Trace)
void PushLastImport(const char* name, PPCContext& ctx) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s r3=0x%llX r4=0x%llX lr=0x%llX",
             name,
             (unsigned long long)ctx.r3.u64,
             (unsigned long long)ctx.r4.u64,
             (unsigned long long)ctx.lr);
    std::lock_guard<std::mutex> lk(g_lastImportsMutex);
    g_lastImports[g_lastImportsHead] = buf;
    g_lastImportsHead = (g_lastImportsHead + 1) % kLastImportsCap;
    if (g_lastImportsCount < kLastImportsCap) g_lastImportsCount++;
}

// Dump do ring buffer (chamado do SegvHandler)
void DumpLastImports(const char* title) {
    std::lock_guard<std::mutex> lk(g_lastImportsMutex);
    int n = g_lastImportsCount;
    if (n == 0) return;
    LogRaw("---- %s (%d ultimos) ----", title, n);
    int start = (g_lastImportsHead - n + kLastImportsCap) % kLastImportsCap;
    for (int i = 0; i < n; i++) {
        int idx = (start + i) % kLastImportsCap;
        LogRaw("  [%2d] %s", i, g_lastImports[idx].c_str());
    }
    LogRaw("---- fim dos ultimos imports ----");
}

uint64_t NowMs() {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

// ---- backtrace via _Unwind_Backtrace (ABI do compilador) ----
//
// O backtrace() do libc so existe a partir da API 33; em API 29 o linker
// nao encontra o simbolo. _Unwind_Backtrace e parte da ABI do compilador
// (libgcc/libunwind) e esta sempre disponivel no NDK.

struct BacktraceState {
    void** current;
    void** end;
};

static _Unwind_Reason_Code UnwindCallback(struct _Unwind_Context* context, void* arg) {
    BacktraceState* state = static_cast<BacktraceState*>(arg);
    uintptr_t pc = _Unwind_GetIP(context);
    if (pc) {
        if (state->current == state->end) {
            return _URC_END_OF_STACK;
        }
        *state->current++ = reinterpret_cast<void*>(pc);
    }
    return _URC_NO_REASON;
}

static int CaptureBacktrace(void** buffer, int max) {
    BacktraceState state = { buffer, buffer + max };
    _Unwind_Backtrace(UnwindCallback, &state);
    return static_cast<int>(state.current - buffer);
}

// ---- helpers ----

uint32_t Get32(const uint8_t* b, uint32_t a) { uint32_t v; memcpy(&v,b+a,4); return __builtin_bswap32(v); }
uint16_t Get16(const uint8_t* b, uint32_t a) { uint16_t v; memcpy(&v,b+a,2); return __builtin_bswap16(v); }
void Put32(uint8_t* b, uint32_t a, uint32_t v) { v=__builtin_bswap32(v); memcpy(b+a,&v,4); }
void Put16(uint8_t* b, uint32_t a, uint16_t v) { v=__builtin_bswap16(v); memcpy(b+a,&v,2); }
void Put64(uint8_t* b, uint32_t a, uint64_t v) { v=__builtin_bswap64(v); memcpy(b+a,&v,8); }

std::string GetCStr(const uint8_t* b, uint32_t a, size_t max=260) {
    std::string r;
    for (size_t i=0;i<max;i++) { char c=(char)b[a+i]; if(!c) break; r.push_back((c>=0x20&&c<0x7F)?c:'?'); }
    return r;
}

uint32_t BumpAlloc(Bump& region, uint32_t size, uint32_t alignment) {
    std::lock_guard<std::mutex> lk(g_memMutex);
    if (alignment < 0x1000) alignment = 0x1000;
    uint64_t aligned = ((uint64_t)region.current + alignment - 1) & ~((uint64_t)alignment - 1);
    uint64_t sized   = ((uint64_t)size + 0xFFF) & ~(uint64_t)0xFFF;
    if (aligned + sized > region.end) return 0;
    region.current = (uint32_t)(aligned + sized);
    return (uint32_t)aligned;
}

bool Commit(uint64_t guest, uint64_t size, const char* what) {
    uint64_t start = guest & ~0xFFFull;
    uint64_t end   = (guest + size + 0xFFF) & ~0xFFFull;
    uint64_t len   = end - start;
    int rc = mprotect(g_base + start, len, PROT_READ|PROT_WRITE);
    if (rc != 0) {
        Log("==== ERRO MapRegions: mprotect(%s) FALHOU: guest=0x%llX-0x%llX (%llu MB) errno=%d (%s) ====",
            what,
            (unsigned long long)start, (unsigned long long)end,
            (unsigned long long)(len >> 20), errno, strerror(errno));
        return false;
    }
    Log("MapRegions: %s guest=0x%llX-0x%llX (%llu MB) host=0x%llX-0x%llX OK",
        what,
        (unsigned long long)start, (unsigned long long)end,
        (unsigned long long)(len >> 20),
        (unsigned long long)(uintptr_t)(g_base + start),
        (unsigned long long)(uintptr_t)(g_base + end));
    return true;
}

void Trace(const char* name, PPCContext& ctx) {
    if (g_stop.load()) throw BootStop{"parado pelo usuario"};
    uint64_t total = ++g_totalCalls;
    g_lastCallMs.store(NowMs(), std::memory_order_relaxed);
    uint64_t count;
    { std::lock_guard<std::mutex> lk(g_callsMutex); count = ++g_callCounts[name]; }
    { std::lock_guard<std::mutex> lk(g_stateMutex); g_lastImport = name; }
    PushLastImport(name, ctx);
    if ((count<=3||count==100||count==10000||count==1000000) && total<4000)
        Log("[import] %s #%llu r3=0x%llX r4=0x%llX lr=0x%llX",
            name,
            (unsigned long long)count,
            (unsigned long long)ctx.r3.u64,
            (unsigned long long)ctx.r4.u64,
            (unsigned long long)ctx.lr);
    if (count > 20000000ull) throw BootStop{std::string("==== importacao em laco: ")+name+" ===="};
}

// ---- traducao de si_code ----

const char* SiCodeName(int signo, int code) {
    switch (code) {
        case 0: return "SI_USER";
        case -1: return "SI_TKILL";
        case -2: return "SI_QUEUE";
        case -3: return "SI_TIMER";
        case -4: return "SI_MESGQ";
        case -5: return "SI_ASYNCIO";
        case -6: return "SI_TKILL";
        case 1: return "SEGV_MAPERR";
        case 2: return "SEGV_ACCERR";
        case 3: return "SEGV_BNDERR";
        case 4: return "SEGV_PKUERR";
        default: return "?";
    }
}

// ---- handler de sinais ----

void SegvHandler(int signo, siginfo_t* info, void* ucv) {
    t_fault.fault_addr = (uint64_t)(uintptr_t)info->si_addr;
    t_fault.fault_address = 0;
    t_fault.signo   = signo;
    t_fault.si_code = info ? info->si_code : 0;

    ucontext_t* uc = (ucontext_t*)ucv;
#if defined(__aarch64__)
    t_fault.pc  = (uint64_t)uc->uc_mcontext.pc;
    t_fault.sp  = (uint64_t)uc->uc_mcontext.sp;
    t_fault.r1  = (uint64_t)uc->uc_mcontext.regs[1];
    t_fault.r2  = (uint64_t)uc->uc_mcontext.regs[2];
    t_fault.r13 = (uint64_t)uc->uc_mcontext.regs[13];
    t_fault.lr  = (uint64_t)uc->uc_mcontext.regs[30];
    t_fault.fault_address = (uint64_t)uc->uc_mcontext.fault_address;
#elif defined(__x86_64__)
    t_fault.pc  = (uint64_t)uc->uc_mcontext.gregs[REG_RIP];
    t_fault.sp  = (uint64_t)uc->uc_mcontext.gregs[REG_RSP];
    t_fault.r1  = (uint64_t)uc->uc_mcontext.gregs[REG_RSP];
    t_fault.r2  = 0;
    t_fault.r13 = 0;
    t_fault.lr  = 0;
#endif

    if (t_fault.fault_addr == 0 && t_fault.fault_address != 0) {
        t_fault.fault_addr = t_fault.fault_address;
    }

    t_fault.pc_so_offset = 0;
    t_fault.so_name[0] = 0;
    t_fault.so_sym[0]  = 0;
    Dl_info di{};
    if (dladdr((void*)(uintptr_t)t_fault.pc, &di) && di.dli_fbase) {
        t_fault.pc_so_offset = t_fault.pc - (uint64_t)(uintptr_t)di.dli_fbase;
        if (di.dli_fname) {
            strncpy(t_fault.so_name, di.dli_fname, sizeof(t_fault.so_name)-1);
            t_fault.so_name[sizeof(t_fault.so_name)-1] = 0;
        }
        if (di.dli_sname) {
            strncpy(t_fault.so_sym, di.dli_sname, sizeof(t_fault.so_sym)-1);
            t_fault.so_sym[sizeof(t_fault.so_sym)-1] = 0;
        }
    }

    // Pilha nativa via _Unwind_Backtrace (identifica quem chamou abort())
    t_fault.bt_size = CaptureBacktrace(t_fault.bt, (int)(sizeof(t_fault.bt)/sizeof(t_fault.bt[0])));

    LogRaw("==== SINAL signo=%d (%s) si_code=%d (%s) addr=0x%llX pc=0x%llX lr=0x%llX ====",
           signo,
           signo==SIGSEGV?"SIGSEGV":signo==SIGABRT?"SIGABRT":signo==SIGBUS?"SIGBUS":
           signo==SIGILL?"SIGILL":signo==SIGTRAP?"SIGTRAP":signo==SIGFPE?"SIGFPE":"?",
           t_fault.si_code, SiCodeName(signo, t_fault.si_code),
           (unsigned long long)t_fault.fault_addr,
           (unsigned long long)t_fault.pc,
           (unsigned long long)t_fault.lr);

    if (t_fault.so_name[0]) {
        LogRaw("==== SINAL: so=%s sym=%s pc_so=+0x%llX ====",
               t_fault.so_name,
               t_fault.so_sym[0] ? t_fault.so_sym : "(sem simbolo)",
               (unsigned long long)t_fault.pc_so_offset);
    }

    // Pilha nativa (backtrace)
    if (t_fault.bt_size > 0) {
        LogRaw("---- BACKTRACE nativo (%d frames) ----", t_fault.bt_size);
        for (int i = 0; i < t_fault.bt_size; i++) {
            Dl_info bdi{};
            const char* bname = "?";
            const char* bsym  = "?";
            uint64_t boff = 0;
            if (dladdr(t_fault.bt[i], &bdi) && bdi.dli_fbase) {
                if (bdi.dli_fname) bname = bdi.dli_fname;
                if (bdi.dli_sname) bsym  = bdi.dli_sname;
                boff = (uint64_t)(uintptr_t)t_fault.bt[i] - (uint64_t)(uintptr_t)bdi.dli_fbase;
            }
            LogRaw("  #%02d 0x%llX  %s  %s+0x%llX",
                   i, (unsigned long long)(uintptr_t)t_fault.bt[i],
                   bname, bsym, (unsigned long long)boff);
        }
        LogRaw("---- fim do backtrace ----");
    }

    DumpLastImports("ultimos imports antes do sinal");

    if (t_jump) siglongjmp(*t_jump, 1);
    _exit(139);
}

void InstallSignalHandler() {
    struct sigaction sa; memset(&sa,0,sizeof(sa));
    sa.sa_sigaction = SegvHandler;
    sa.sa_flags = SA_SIGINFO|SA_NODEFER|SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGSEGV);
    sigaddset(&sa.sa_mask, SIGBUS);
    sigaddset(&sa.sa_mask, SIGILL);
    sigaddset(&sa.sa_mask, SIGABRT);
    sigaddset(&sa.sa_mask, SIGTRAP);
    sigaddset(&sa.sa_mask, SIGFPE);

    sigaction(SIGSEGV,&sa,nullptr);
    sigaction(SIGBUS, &sa,nullptr);
    sigaction(SIGILL, &sa,nullptr);
    sigaction(SIGABRT,&sa,nullptr);
    sigaction(SIGTRAP,&sa,nullptr);
    sigaction(SIGFPE, &sa,nullptr);
    Log("Sinais instalados: SIGSEGV SIGBUS SIGILL SIGABRT SIGTRAP SIGFPE");
}

// ---- handler de terminate ----

void TerminateHandler() {
    std::string msg = "(sem excecao ativa)";
    try {
        std::exception_ptr ep = std::current_exception();
        if (ep) {
            try {
                std::rethrow_exception(ep);
            } catch (const std::exception& e) {
                msg = e.what();
            } catch (const BootStop& b) {
                msg = "BootStop: " + b.reason;
            } catch (...) {
                msg = "(excecao desconhecida)";
            }
        }
    } catch (...) {}

    LogRaw("==== TERMINATE: %s ====", msg.c_str());
    LogRaw("==== TERMINATE: chamando abort() (o processo vai morrer) ====");
    std::abort();
}

void NewHandler() {
    LogRaw("==== NEW HANDLER: bad_alloc nao tratado ====");
    std::abort();
}

// ---- arena ----

bool ReserveArena() {
    void* m = mmap(nullptr, kArenaSize+0x10000, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
    if (m == MAP_FAILED) {
        Log("==== ERRO ReserveArena: nao consegui reservar 4 GB (errno=%d) ====", errno);
        return false;
    }
    g_base = (uint8_t*)m;
    Log("Memoria reservada em %p (host), guest base = 0x%llX",
        m, (unsigned long long)PPC_IMAGE_BASE);
    Log("kArenaSize=0x%llX kArenaEnd(host)=0x%llX",
        (unsigned long long)kArenaSize,
        (unsigned long long)(uintptr_t)(g_base + kArenaSize));
    return true;
}

bool MapRegions() {
    if (!Commit(0, kArenaSize, "arena inteira")) return false;
    return true;
}

bool LoadXex(const std::string& path, uint32_t& entry) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        Log("==== ERRO LoadXex: nao achei %s ====", path.c_str());
        return false;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Log("default.xex: %zu bytes", data.size());
    Image image = Image::ParseImage(data.data(), data.size());
    Log("base=0x%llX entrada=0x%llX secoes=%llu",
        (unsigned long long)image.base, (unsigned long long)image.entry_point,
        (unsigned long long)image.sections.size());
    if (image.base != PPC_IMAGE_BASE) {
        Log("==== ERRO LoadXex: base do XEX (0x%llX) != PPC_IMAGE_BASE (0x%llX) ====",
            (unsigned long long)image.base, (unsigned long long)PPC_IMAGE_BASE);
        return false;
    }
    size_t copied = 0;
    for (const auto& s : image.sections) {
        if (!s.data || !s.size) continue;
        if (s.base < PPC_IMAGE_BASE || s.base+s.size > PPC_IMAGE_BASE+PPC_IMAGE_SIZE) {
            Log("secao IGNORADA: base=0x%llX size=0x%llX (fora da imagem)",
                (unsigned long long)s.base, (unsigned long long)s.size);
            continue;
        }
        memcpy(g_base+s.base, s.data, s.size); copied += s.size;
    }
    Log("Copiados %zu bytes para a memoria do jogo", copied);
    entry = (uint32_t)image.entry_point;
    return true;
}

void InitFunctionTable() {
    size_t mapped = 0, skipped_guest = 0, skipped_null = 0;
    for (size_t i=0; PPCFuncMappings[i].guest!=0; i++) {
        uint64_t g = PPCFuncMappings[i].guest;
        if (!PPCFuncMappings[i].host) { skipped_null++; continue; }
        if (g >= PPC_CODE_BASE && g < PPC_CODE_BASE+PPC_CODE_SIZE) {
            PPC_LOOKUP_FUNC(g_base, g) = PPCFuncMappings[i].host; mapped++;
        } else {
            skipped_guest++;
        }
    }
    Log("Tabela de funcoes: %zu entradas mapeadas (skip: %zu sem host, %zu fora de PPC_CODE_BASE..BASE+SIZE)",
        mapped, skipped_null, skipped_guest);
    Log("kTableStart(guest)=0x%llX kTableSize=0x%llX",
        (unsigned long long)kTableStart, (unsigned long long)kTableSize);
}

void RunGuest(PPCFunc* fn, PPCContext& ctx) {
    sigjmp_buf jump; t_jump = &jump;
    int jmp_ret = sigsetjmp(jump,1);
    if (jmp_ret == 0) {
        try {
            fn(ctx, g_base);
            std::lock_guard<std::mutex> lk(g_stateMutex);
            g_endReason = "a entrada do jogo retornou (normal)"; g_state = 2;
        } catch (const BootStop& s) {
            std::lock_guard<std::mutex> lk(g_stateMutex);
            g_endReason = s.reason; g_state = 2;
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lk(g_stateMutex);
            g_endReason = std::string("==== excecao: ")+e.what()+" ===="; g_state = 3;
        } catch (...) {
            std::lock_guard<std::mutex> lk(g_stateMutex);
            g_endReason = "==== excecao desconhecida ===="; g_state = 3;
        }
    } else {
        // Retornou do siglongjmp (sinal capturado)
        uint64_t fault      = t_fault.fault_addr;
        uint64_t fault_arm  = t_fault.fault_address;
        uint64_t pc         = t_fault.pc;
        uint64_t pc_off     = t_fault.pc_so_offset;

        const char* so  = t_fault.so_name[0] ? t_fault.so_name : "(desconhecido)";
        const char* sym = t_fault.so_sym[0]  ? t_fault.so_sym  : "(sem simbolo)";

        const char* region = "FORA de todas as regioes";
        uint64_t rel = 0;
        if (fault >= PPC_IMAGE_BASE && fault < kImageGlobalsEnd) {
            region = "imagem+globais"; rel = fault - PPC_IMAGE_BASE;
        } else if (fault >= kHeapStart && fault < kHeapEnd) {
            region = "heap"; rel = fault - kHeapStart;
        } else if (fault >= kPhysStart && fault < kPhysEnd) {
            region = "fisica"; rel = fault - kPhysStart;
        } else if (fault >= 0 && fault < kArenaSize) {
            region = "arena (outra faixa)"; rel = fault;
        }

        char text[1400];
        snprintf(text, sizeof(text),
            "==== falha: sinal=%d si_code=%d fault_addr=0x%llX fault_address=0x%llX "
            "regiao=%s(+0x%llX) PC_abs=0x%llX PC_so=+0x%llX so=%s sym=%s "
            "r1=0x%llX r2=0x%llX r13=0x%llX lr=0x%llX sp=0x%llX "
            "base_host=0x%llX ultima_funcao='%s' ====",
            t_fault.signo, t_fault.si_code,
            (unsigned long long)fault, (unsigned long long)fault_arm,
            region, (unsigned long long)rel,
            (unsigned long long)pc, (unsigned long long)pc_off, so, sym,
            (unsigned long long)t_fault.r1, (unsigned long long)t_fault.r2,
            (unsigned long long)t_fault.r13, (unsigned long long)t_fault.lr,
            (unsigned long long)t_fault.sp,
            (unsigned long long)(uintptr_t)g_base,
            g_lastImport.c_str());
        std::lock_guard<std::mutex> lk(g_stateMutex);
        g_endReason = text;
        g_state = 3;
    }
    g_finished.store(true);
    t_jump = nullptr;
}

struct StartArgs { std::string gameDir; std::string logPath; };

void* BootThread(void* raw) {
    std::unique_ptr<StartArgs> args((StartArgs*)raw);

    g_logFile = fopen(args->logPath.c_str(), "w");
    g_logFd   = open(args->logPath.c_str(), O_WRONLY|O_CREAT|O_APPEND, 0644);

    Log("=== BO1 runtime inicial (v08h) ===");
    Log("Pasta: %s", args->gameDir.c_str());
    Log("ppc_config.h: PPC_IMAGE_BASE=0x%llX PPC_IMAGE_SIZE=0x%llX",
        (unsigned long long)PPC_IMAGE_BASE, (unsigned long long)PPC_IMAGE_SIZE);
    Log("ppc_config.h: PPC_CODE_BASE=0x%llX PPC_CODE_SIZE=0x%llX",
        (unsigned long long)PPC_CODE_BASE, (unsigned long long)PPC_CODE_SIZE);
    Log("Constantes runtime: kArenaSize=0x%llX kHeapStart=0x%llX kHeapEnd=0x%llX "
        "kPhysStart=0x%llX kPhysEnd=0x%llX kImageGlobalsEnd=0x%llX",
        (unsigned long long)kArenaSize,
        (unsigned long long)kHeapStart, (unsigned long long)kHeapEnd,
        (unsigned long long)kPhysStart, (unsigned long long)kPhysEnd,
        (unsigned long long)kImageGlobalsEnd);

    std::set_terminate(TerminateHandler);
    std::set_new_handler(NewHandler);
    Log("Handlers instalados: terminate + new_handler");

    InstallSignalHandler();
    uint32_t entry = 0;
    bool ok = ReserveArena() && MapRegions() && LoadXex(args->gameDir+"/default.xex", entry);
    if (!ok) {
        std::lock_guard<std::mutex> lk(g_stateMutex);
        g_endReason = "==== falha na preparacao ===="; g_state = 3;
        LogRaw("=== Fim: %s ===", g_endReason.c_str());
        g_finished.store(true);
        return nullptr;
    }

    // Stub blr: aloca 1 pagina no heap, escreve "blr" (0x4E800020) no inicio.
    // Usado por __imp__XexGetProcedureAddress quando o ordinal nao existe.
    g_blr_stub_addr = BumpAlloc(g_heap, 0x1000, 0x1000);
    if (g_blr_stub_addr) {
        uint32_t blr = __builtin_bswap32(0x4E800020u);
        memcpy(g_base + g_blr_stub_addr, &blr, 4);
        Log("blr stub em 0x%X", g_blr_stub_addr);
    } else {
        Log("==== ERRO: nao consegui alocar blr stub ====");
    }

    InitFunctionTable();
    PPCFunc* fn = PPC_LOOKUP_FUNC(g_base, entry);
    if (!fn) {
        Log("==== ERRO: entrada 0x%X sem funcao recompilada ====", entry);
        std::lock_guard<std::mutex> lk(g_stateMutex);
        g_endReason = "==== entrada sem funcao recompilada ===="; g_state = 3;
        LogRaw("=== Fim: %s ===", g_endReason.c_str());
        g_finished.store(true);
        return nullptr;
    }
    uint32_t stack  = BumpAlloc(g_heap, 8*1024*1024, 0x10000);
    uint32_t pcr    = BumpAlloc(g_heap, 0x1000, 0x1000);
    uint32_t thread = BumpAlloc(g_heap, 0x1000, 0x1000);
    if (!stack||!pcr||!thread) {
        Log("==== ERRO: sem memoria para a pilha (stack=0x%X pcr=0x%X thread=0x%X) ====",
            stack, pcr, thread);
        std::lock_guard<std::mutex> lk(g_stateMutex);
        g_endReason = "==== sem memoria para a pilha ===="; g_state = 3;
        LogRaw("=== Fim: %s ===", g_endReason.c_str());
        g_finished.store(true);
        return nullptr;
    }
    Put32(g_base, pcr+256, thread);
    g_base[pcr+268] = 0;

    PPCContext ctx{};
    ctx.fpscr.loadFromHost();
    ctx.r1.u64  = stack + 8*1024*1024 - 0x1000;
    ctx.r13.u64 = pcr;

    Log("Pilha(guest)=0x%X (topo=0x%llX) pcr=0x%X thread=0x%X entrada=0x%X",
        stack, (unsigned long long)ctx.r1.u64, pcr, thread, entry);
    Log("ctx.r2 (TOC) = 0x%llX  (nao usado pelo _xstart nas primeiras 60 linhas)",
        (unsigned long long)ctx.r2.u64);
    Log("ctx.r13 (PCR) = 0x%llX  PCR+256(host)=0x%llX PCR+268(host)=0x%llX",
        (unsigned long long)ctx.r13.u64,
        (unsigned long long)(uintptr_t)(g_base + pcr + 256),
        (unsigned long long)(uintptr_t)(g_base + pcr + 268));

    Log("Chamando a entrada do jogo...");
    g_state = 1;
    g_lastCallMs.store(NowMs(), std::memory_order_relaxed);

    std::thread beat([](){
        while (!g_finished.load()) {
            for (int s=0; s<5 && !g_finished.load(); s++)
                std::this_thread::sleep_for(std::chrono::seconds(1));
            if (g_finished.load()) break;
            uint64_t last = g_lastCallMs.load(std::memory_order_relaxed);
            uint64_t now  = NowMs();
            uint64_t secs = (now > last) ? (now - last)/1000ull : 0;
            std::string imp;
            { std::lock_guard<std::mutex> lk(g_stateMutex); imp = g_lastImport; }
            if (secs >= 30) {
                LogRaw("[TRAVADO] %llu s sem import · chamadas: %llu ultima: '%s'",
                       (unsigned long long)secs,
                       (unsigned long long)g_totalCalls.load(),
                       imp.c_str());
            } else {
                LogRaw("[beat] %llu s desde ultimo import · chamadas: %llu ultima: '%s'",
                       (unsigned long long)secs,
                       (unsigned long long)g_totalCalls.load(),
                       imp.c_str());
            }
        }
    });
    beat.detach();

    RunGuest(fn, ctx);

    std::string reason;
    { std::lock_guard<std::mutex> lk(g_stateMutex); reason=g_endReason; }
    Log("=== Fim: %s ===", reason.c_str());
    LogRaw("=== Fim (raw): %s ===", reason.c_str());
    Log("Total de chamadas ao kernel: %llu", (unsigned long long)g_totalCalls.load());
    {
        std::lock_guard<std::mutex> lk(g_callsMutex);
        std::vector<std::pair<std::string,uint64_t>> items(g_callCounts.begin(),g_callCounts.end());
        std::sort(items.begin(),items.end(),[](const auto& a,const auto& b){return a.second>b.second;});
        Log("Funcoes do kernel chamadas (%zu diferentes):", items.size());
        for (size_t i=0;i<items.size()&&i<25;i++)
            Log("  %s x%llu", items[i].first.c_str(), (unsigned long long)items[i].second);
    }
    return nullptr;
}

std::mutex g_tlsMutex;
uint32_t g_tlsNext = 0;
uint32_t g_tlsValues[256];
uint32_t g_nextHandle = 0x1000;

uint32_t AllocHandle() {
    std::lock_guard<std::mutex> lk(g_tlsMutex);
    uint32_t h = g_nextHandle;
    g_nextHandle += 4;
    return h;
}

} // namespace

// --- funcoes do kernel ---

// Wrapper extern "C" para os .cpp gerados (patch_instrument.py injeta
// bo1_LogRaw(...) dentro das funcoes sub_824D1EE8, sub_825FC8F0,
// sub_82604428, sub_82603680).
//
// Log de confirmacao: as primeiras 20 chamadas aparecem como
// "[bo1_LogRaw] chamada #N". Se nao aparecer nenhuma, os hooks NAO
// estao sendo chamados.
extern "C" void bo1_LogRaw(const char* fmt, ...) {
    static std::atomic<int> call_count{0};
    int n = ++call_count;
    if (n <= 20) {
        LogRaw("[bo1_LogRaw] chamada #%d", n);
    }
    char text[2048];
    va_list args; va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    LogRaw("%s", text);
}

void bo1_import_stub(const char* name, PPCContext& ctx, uint8_t* /*base*/) {
    Trace(name, ctx); ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__KeQuerySystemTime) {
    Trace("KeQuerySystemTime", ctx);
    struct timeval tv; gettimeofday(&tv, nullptr);
    uint64_t ft = ((uint64_t)tv.tv_sec*10000000ull)+((uint64_t)tv.tv_usec*10ull)+116444736000000000ull;
    if (ctx.r3.u32) Put64(base, ctx.r3.u32, ft);
}
PPC_FUNC(__imp__KeQueryPerformanceFrequency) { Trace("KeQueryPerformanceFrequency",ctx); ctx.r3.u64=50000000ull; }
PPC_FUNC(__imp__KeGetCurrentProcessType)     { Trace("KeGetCurrentProcessType",ctx);     ctx.r3.u64=1; }

PPC_FUNC(__imp__KeTlsAlloc)   { Trace("KeTlsAlloc",ctx);   std::lock_guard<std::mutex> lk(g_tlsMutex); ctx.r3.u64=(g_tlsNext<256)?g_tlsNext++:0xFFFFFFFFu; }
PPC_FUNC(__imp__KeTlsFree)    { Trace("KeTlsFree",ctx);    ctx.r3.u64=1; }
PPC_FUNC(__imp__KeTlsGetValue){ Trace("KeTlsGetValue",ctx);std::lock_guard<std::mutex> lk(g_tlsMutex); ctx.r3.u64=(ctx.r3.u32<256)?g_tlsValues[ctx.r3.u32]:0; }
PPC_FUNC(__imp__KeTlsSetValue){ Trace("KeTlsSetValue",ctx);std::lock_guard<std::mutex> lk(g_tlsMutex); if(ctx.r3.u32<256) g_tlsValues[ctx.r3.u32]=ctx.r4.u32; ctx.r3.u64=1; }

PPC_FUNC(__imp__RtlInitializeCriticalSection)             { Trace("RtlInitializeCriticalSection",ctx);             ctx.r3.u64=0; }
PPC_FUNC(__imp__RtlInitializeCriticalSectionAndSpinCount) { Trace("RtlInitializeCriticalSectionAndSpinCount",ctx); ctx.r3.u64=0; }
PPC_FUNC(__imp__RtlEnterCriticalSection)                  { Trace("RtlEnterCriticalSection",ctx); }
PPC_FUNC(__imp__RtlLeaveCriticalSection)                  { Trace("RtlLeaveCriticalSection",ctx); }
PPC_FUNC(__imp__RtlTryEnterCriticalSection)               { Trace("RtlTryEnterCriticalSection",ctx); ctx.r3.u64=1; }

PPC_FUNC(__imp__NtAllocateVirtualMemory) {
    Trace("NtAllocateVirtualMemory",ctx);
    uint32_t ap=ctx.r3.u32, sp=ctx.r4.u32;
    uint32_t a=Get32(base,ap), s=Get32(base,sp);
    uint32_t r=a;
    if (a<kHeapStart||a>=kHeapEnd) r=BumpAlloc(g_heap,s,0x10000);
    if (!r) { ctx.r3.u64=0xC0000017u; return; }
    Put32(base,ap,r); Put32(base,sp,(s+0xFFF)&~0xFFFu); ctx.r3.u64=0;
}
PPC_FUNC(__imp__NtFreeVirtualMemory)       { Trace("NtFreeVirtualMemory",ctx);       ctx.r3.u64=0; }
PPC_FUNC(__imp__MmAllocatePhysicalMemoryEx){
    Trace("MmAllocatePhysicalMemoryEx",ctx);
    uint32_t al=ctx.r8.u32;
    uint32_t r=BumpAlloc(g_phys,ctx.r4.u32,al?al:0x1000);
    if (!r) LogRaw("[aviso] MmAllocatePhysicalMemoryEx size=0x%X al=0x%X -> FALHOU (bump cheio)",
                   ctx.r4.u32, al);
    ctx.r3.u64=r;
}
PPC_FUNC(__imp__MmFreePhysicalMemory)      { Trace("MmFreePhysicalMemory",ctx); }
PPC_FUNC(__imp__MmGetPhysicalAddress)      { Trace("MmGetPhysicalAddress",ctx);       ctx.r3.u64=ctx.r3.u32&0x1FFFFFFFu; }
PPC_FUNC(__imp__MmQueryAddressProtect)     { Trace("MmQueryAddressProtect",ctx);      ctx.r3.u64=4; }
PPC_FUNC(__imp__ExAllocatePool)            { Trace("ExAllocatePool",ctx);             ctx.r3.u64=BumpAlloc(g_heap,ctx.r3.u32,0x1000); }
PPC_FUNC(__imp__ExAllocatePoolTypeWithTag) { Trace("ExAllocatePoolTypeWithTag",ctx);  ctx.r3.u64=BumpAlloc(g_heap,ctx.r3.u32,0x1000); }
PPC_FUNC(__imp__ExFreePool)                { Trace("ExFreePool",ctx); }

PPC_FUNC(__imp__RtlInitAnsiString) {
    Trace("RtlInitAnsiString",ctx);
    uint32_t d=ctx.r3.u32, s=ctx.r4.u32, len=0;
    if (s) while(len<0xFFFE&&base[s+len]) len++;
    Put16(base,d,(uint16_t)len); Put16(base,d+2,(uint16_t)(s?len+1:0)); Put32(base,d+4,s);
}
PPC_FUNC(__imp__RtlInitUnicodeString) {
    Trace("RtlInitUnicodeString",ctx);
    uint32_t d=ctx.r3.u32, s=ctx.r4.u32, len=0;
    if (s) while(len<0xFFFC&&(base[s+len]||base[s+len+1])) len+=2;
    Put16(base,d,(uint16_t)len); Put16(base,d+2,(uint16_t)(s?len+2:0)); Put32(base,d+4,s);
}

PPC_FUNC(__imp__NtCreateEvent) {
    Trace("NtCreateEvent",ctx);
    if (ctx.r3.u32) Put32(base,ctx.r3.u32,AllocHandle());
    ctx.r3.u64=0;
}
PPC_FUNC(__imp__NtCreateSemaphore) {
    Trace("NtCreateSemaphore",ctx);
    if (ctx.r3.u32) Put32(base,ctx.r3.u32,AllocHandle());
    ctx.r3.u64=0;
}
PPC_FUNC(__imp__NtCreateMutant) {
    Trace("NtCreateMutant",ctx);
    if (ctx.r3.u32) Put32(base,ctx.r3.u32,AllocHandle());
    ctx.r3.u64=0;
}
PPC_FUNC(__imp__NtDuplicateObject) {
    Trace("NtDuplicateObject",ctx);
    if (ctx.r5.u32) Put32(base,ctx.r5.u32,AllocHandle());
    ctx.r3.u64=0;
}
PPC_FUNC(__imp__NtSetEvent)              { Trace("NtSetEvent",ctx);              ctx.r3.u64=0; }
PPC_FUNC(__imp__NtClearEvent)            { Trace("NtClearEvent",ctx);            ctx.r3.u64=0; }
PPC_FUNC(__imp__KeSetEvent)              { Trace("KeSetEvent",ctx);              ctx.r3.u64=0; }
PPC_FUNC(__imp__KeResetEvent)            { Trace("KeResetEvent",ctx);            ctx.r3.u64=0; }
PPC_FUNC(__imp__KeWaitForSingleObject)   { Trace("KeWaitForSingleObject",ctx);   ctx.r3.u64=0; }
PPC_FUNC(__imp__NtWaitForSingleObjectEx) { Trace("NtWaitForSingleObjectEx",ctx); ctx.r3.u64=0; }

PPC_FUNC(__imp__ExCreateThread) {
    Trace("ExCreateThread",ctx);
    if (ctx.r3.u32) Put32(base,ctx.r3.u32,AllocHandle());
    ctx.r3.u64=0;
}
PPC_FUNC(__imp__KeSetAffinityThread)    { Trace("KeSetAffinityThread",ctx);    ctx.r3.u64=0; }
PPC_FUNC(__imp__KeSetBasePriorityThread){ Trace("KeSetBasePriorityThread",ctx); ctx.r3.u64=0; }

PPC_FUNC(__imp__RtlRaiseException) {
    Trace("RtlRaiseException",ctx);
    uint32_t er = ctx.r3.u32;
    if (er) {
        uint32_t code   = Get32(base, er + 0);
        uint32_t nparam = Get32(base, er + 24);
        LogRaw("[exception] RtlRaiseException r3=0x%X code=0x%X nparam=%u",
               er, code, nparam);
        uint32_t info[4] = {0,0,0,0};
        for (uint32_t i = 0; i < nparam && i < 4; i++) {
            info[i] = Get32(base, er + 28 + i*4);
        }
        LogRaw("[exception]   info: 0x%X 0x%X 0x%X 0x%X",
               info[0], info[1], info[2], info[3]);
    } else {
        LogRaw("[exception] RtlRaiseException r3=0 (sem ExceptionRecord)");
    }
    ctx.r3.u64=0;
}

PPC_FUNC(__imp__NtCreateFile) {
    Trace("NtCreateFile",ctx);
    uint32_t attr=ctx.r5.u32; std::string path="?";
    if (attr) {
        uint32_t ns=Get32(base,attr+4);
        if (ns) {
            uint16_t len=Get16(base,ns); uint32_t buf=Get32(base,ns+4);
            if (buf) { path.clear(); for(uint16_t i=0;i<len&&i<260;i++){char c=(char)base[buf+i];path.push_back((c>=0x20&&c<0x7F)?c:'?');} }
        }
    }
    Log("[arquivo] NtCreateFile: %s", path.c_str());
    ctx.r3.u64=0xC0000034u;
}

PPC_FUNC(__imp__DbgPrint)             { Trace("DbgPrint",ctx); Log("[DbgPrint] %s",GetCStr(base,ctx.r3.u32).c_str()); ctx.r3.u64=0; }
PPC_FUNC(__imp__HalReturnToFirmware)  { Trace("HalReturnToFirmware",ctx);  throw BootStop{"==== o jogo chamou HalReturnToFirmware ===="}; }
PPC_FUNC(__imp__KeBugCheck)           { Trace("KeBugCheck",ctx);           throw BootStop{"==== o jogo chamou KeBugCheck ===="}; }
PPC_FUNC(__imp__KeBugCheckEx)         {
    Trace("KeBugCheckEx",ctx);
    LogRaw("==== KeBugCheckEx: r3=0x%X r4=0x%X r5=0x%X r6=0x%X ====",
           ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32);
    throw BootStop{"==== o jogo chamou KeBugCheckEx ===="};
}

// --- XAM / XEX / XNET ---

PPC_FUNC(__imp__XexCheckExecutablePrivilege) {
    Trace("XexCheckExecutablePrivilege",ctx);
    ctx.r3.u64 = 1;
}
PPC_FUNC(__imp__XexLoadImage) {
    Trace("XexLoadImage",ctx);
    ctx.r3.u64 = 0xC0000034u;
}
PPC_FUNC(__imp__XexGetProcedureAddress) {
    Trace("XexGetProcedureAddress",ctx);
    // Stub blr: em vez de escrever 0 em *r5, escreve o endereco do stub
    // (que e um "blr" e simplesmente retorna). O status de erro
    // 0xC0000263 continua sendo retornado em r3.
    if (ctx.r5.u32 != 0) {
        Put32(base, ctx.r5.u32, g_blr_stub_addr);
    }
    ctx.r3.u64 = 0xC0000263u; // STATUS_PROCEDURE_NOT_FOUND
}
PPC_FUNC(__imp__XexUnloadImage) {
    Trace("XexUnloadImage",ctx);
    ctx.r3.u64 = 0;
}
PPC_FUNC(__imp__RtlNtStatusToDosError) {
    Trace("RtlNtStatusToDosError",ctx);
    uint32_t st = ctx.r3.u32;
    uint32_t dos = 87;
    if (st == 0xC0000034u) dos = 2;
    else if (st == 0xC0000263u) dos = 2;
    else if (st == 0xC000007Fu) dos = 2;
    else if (st == 0xC0000001u) dos = 1;
    else if (st == 0) dos = 0;
    ctx.r3.u64 = dos;
}

PPC_FUNC(__imp__XamGetSystemVersion) {
    Trace("XamGetSystemVersion",ctx);
    ctx.r3.u64 = 0x020044C70000ull;
}

PPC_FUNC(__imp__XamLoaderGetLaunchData) {
    Trace("XamLoaderGetLaunchData",ctx);
    ctx.r3.u64 = 0;
}
PPC_FUNC(__imp__XamLoaderSetLaunchData) {
    Trace("XamLoaderSetLaunchData",ctx);
    ctx.r3.u64 = 0;
}
PPC_FUNC(__imp__XamLoaderLaunchTitle) {
    Trace("XamLoaderLaunchTitle",ctx);
    LogRaw("[XamLoader] LaunchTitle chamado (r3=0x%X r4=0x%X) — retornando sucesso sem efeito real",
           ctx.r3.u32, ctx.r4.u32);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__XGetVideoMode) {
    Trace("XGetVideoMode",ctx);
    uint32_t p = ctx.r3.u32;
    if (p) {
        Put32(base, p+0x00, 1280);
        Put32(base, p+0x04, 720);
        Put32(base, p+0x08, 60);
        Put32(base, p+0x0C, 0x1);
        Put32(base, p+0x10, 0);
        Put32(base, p+0x14, 0);
    }
    ctx.r3.u64 = 1;
}

PPC_FUNC(__imp__XMsgInProcessCall) {
    Trace("XMsgInProcessCall",ctx);
    ctx.r3.u64 = 0;
}
PPC_FUNC(__imp__XamAlloc) {
    Trace("XamAlloc",ctx);
    uint32_t size = ctx.r4.u32;
    ctx.r3.u64 = BumpAlloc(g_heap, size, 0x1000);
}
PPC_FUNC(__imp__XamFree) {
    Trace("XamFree",ctx);
    ctx.r3.u64 = 0;
}

PPC_FUNC(__imp__NetDll_XNetStartup)  { Trace("NetDll_XNetStartup",ctx);  ctx.r3.u64=0; }
PPC_FUNC(__imp__NetDll_WSAStartup)   { Trace("NetDll_WSAStartup",ctx);   ctx.r3.u64=0; }

// --- JNI ---

extern "C" JNIEXPORT jstring JNICALL
Java_com_bo1recomp_NativeRuntime_nativeStart(JNIEnv* env, jclass, jstring gameDir, jstring logPath)
{
    if (g_started.exchange(true))
        return env->NewStringUTF("O runtime so pode ser iniciado uma vez. Feche e abra o app para repetir.");
    const char* gd = env->GetStringUTFChars(gameDir, nullptr);
    const char* lp = env->GetStringUTFChars(logPath, nullptr);
    auto* args = new StartArgs{gd, lp};
    env->ReleaseStringUTFChars(gameDir, gd);
    env->ReleaseStringUTFChars(logPath, lp);
    g_state = 1;
    pthread_attr_t attr; pthread_attr_init(&attr); pthread_attr_setstacksize(&attr, 128u*1024u*1024u);
    pthread_t t;
    if (pthread_create(&t, &attr, BootThread, args)!=0) {
        delete args; g_state=3;
        return env->NewStringUTF("==== Nao consegui criar a thread do runtime ====");
    }
    pthread_detach(t); pthread_attr_destroy(&attr);
    return env->NewStringUTF("Runtime iniciado (v08h). Acompanhe o log abaixo.");
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_bo1recomp_NativeRuntime_nativeState(JNIEnv* env, jclass)
{
    std::string text; int state=g_state.load();
    std::lock_guard<std::mutex> lk(g_stateMutex);
    if (state==0)      text="parado";
    else if (state==1) text="executando · chamadas: "+std::to_string(g_totalCalls.load())+" · ultima: "+g_lastImport;
    else               text=std::string(state==2?"terminou":"erro")+": "+g_endReason;
    return env->NewStringUTF(text.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_bo1recomp_NativeRuntime_nativeStop(JNIEnv*, jclass) { g_stop=true; }

#endif // BO1_HAVE_GENERATED
