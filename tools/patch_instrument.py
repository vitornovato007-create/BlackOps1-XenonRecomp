#!/usr/bin/env python3
"""
patch_instrument.py — injeta hooks de log (bo1_LogRaw) nos .cpp gerados
pelo XenonRecomp, para diagnosticar onde o abort() acontece.

Funcoes alvo:
  sub_824D1EE8  — chamada por sub_82603680 com r5=0xFFFFFFFF.
                  Possui caminho que chama KeBugCheckEx.
  sub_825FC8F0  — ponto final do abort (tail call para sub_825FC6F8,
                  que e noreturn / exit/abort).
  sub_82604428  — decide chamar sub_825FC8F0(17) quando *(base-20568)==0.
  sub_82603680  — chama sub_824D1EE8(??, 0, 0xFFFFFFFF).

Uso:
  python3 patch_instrument.py <generated/ppc> <report.txt>

Idempotente: se o arquivo ja tem o marcador "[instrument]", pula.

Relatorio gerado (formato lido por post_instrument.sh):
  PATCHED: sub_XXXX
  HOOK:    sub_XXXX <descricao>
  MISSING: sub_XXXX
"""

import os
import re
import sys

MARKER = "[instrument]"

# Funcoes alvo -> lista de hooks que devem ser injetados.
# Cada hook eh (tipo, descricao, padrao_de_busca, texto_injetado)
# tipo:
#   "prologue" -> injeta logo apos PPC_FUNC_PROLOGUE();
#   "before"   -> injeta antes de uma linha que casa com padrao_de_busca
#
# Padrao de busca eh uma string literal (substring) que deve aparecer
# em uma linha do arquivo, ou None para prologue.

HOOKS = {
    "sub_824D1EE8": [
        (
            "prologue",
            "entrada r3/r4/r5/r6",
            None,
            'bo1_LogRaw("[hook] sub_824D1EE8 r3=0x%X r4=0x%X r5=0x%X r6=0x%X", '
            'ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32);',
        ),
        (
            "before",
            "antes de KeBugCheckEx",
            "__imp__KeBugCheckEx(ctx, base);",
            'bo1_LogRaw("[hook] sub_824D1EE8 VAI CHAMAR KeBugCheckEx! '
            'r3=0x%X *(r3+20)=0x%X", ctx.r3.u32, '
            '(ctx.r3.u32 ? PPC_LOAD_U32(ctx.r3.u32 + 20) : 0));',
        ),
    ],
    "sub_825FC8F0": [
        (
            "prologue",
            "entrada arg (17)",
            None,
            'bo1_LogRaw("[hook] sub_825FC8F0 arg=%d -> chamando sub_825FC6F8(1,0) [exit]", '
            'ctx.r3.u32);',
        ),
    ],
    "sub_82604428": [
        (
            "prologue",
            "entrada index",
            None,
            'bo1_LogRaw("[hook] sub_82604428 index=%d (r3=0x%X)", '
            'ctx.r3.u32, ctx.r3.u32);',
        ),
        (
            "before",
            "antes de sub_825FC8F0(17)",
            "sub_825FC8F0(ctx, base);",
            'bo1_LogRaw("[hook] sub_82604428 VAI CHAMAR sub_825FC8F0(17) — *(base-20568)==0");',
        ),
    ],
    "sub_82603680": [
        (
            "prologue",
            "entrada r3 (0xFFFFFFFF?)",
            None,
            'bo1_LogRaw("[hook] sub_82603680 r3=0x%X", ctx.r3.u32);',
        ),
        (
            "before",
            "antes de sub_824D1EE8",
            "sub_824D1EE8(ctx, base);",
            'bo1_LogRaw("[hook] sub_82603680 VAI CHAMAR sub_824D1EE8 com r5=0x%X", ctx.r5.u32);',
        ),
    ],
}

# Header injetado no topo de cada arquivo patchado (uma vez so).
HEADER_INJECT = (
    "// [instrument] hook de log para diagnostico do abort()\n"
    'extern "C" void bo1_LogRaw(const char* fmt, ...);\n'
)


def find_impl_line(lines, func_name):
    """Retorna o indice da linha que contem PPC_FUNC_IMPL(__imp__<func_name>)"""
    needle = "__imp__" + func_name + ")"
    for i, line in enumerate(lines):
        if "PPC_FUNC_IMPL" in line and needle in line:
            return i
    return -1


def find_prologue_line(lines, start):
    """Retorna o indice da linha que contem PPC_FUNC_PROLOGUE(); a partir de start"""
    for i in range(start, len(lines)):
        if "PPC_FUNC_PROLOGUE();" in lines[i]:
            return i
    return -1


def file_already_patched(lines):
    return any(MARKER in line for line in lines[:30])


def patch_function(lines, func_name, hooks):
    """
    Aplica os hooks de uma funcao. Retorna (linhas_modificadas, lista_hooks_aplicados).
    """
    applied = []

    impl_idx = find_impl_line(lines, func_name)
    if impl_idx < 0:
        return lines, applied

    prologue_idx = find_prologue_line(lines, impl_idx)
    if prologue_idx < 0:
        return lines, applied

    # Trabalha de baixo pra cima pra nao baguncar indices.
    # 1) Hooks "before" (busca a linha alvo a partir do prologue)
    for (kind, desc, pattern, code) in hooks:
        if kind != "before":
            continue
        target_idx = -1
        for i in range(prologue_idx + 1, len(lines)):
            if pattern in lines[i]:
                target_idx = i
                break
        if target_idx < 0:
            continue
        # Ja tem hook aqui?
        if target_idx > 0 and MARKER in lines[target_idx - 1]:
            applied.append((desc, "ja-presente"))
            continue
        indent = re.match(r"(\s*)", lines[target_idx]).group(1)
        lines.insert(target_idx, f"{indent}{code}\n")
        applied.append((desc, "injetado"))

    # 2) Hooks "prologue" (injeta logo apos PPC_FUNC_PROLOGUE();)
    # Recarrega prologue_idx porque inserimos linhas antes
    prologue_idx = find_prologue_line(lines, impl_idx)
    prologue_hooks = [h for h in hooks if h[0] == "prologue"]
    # Inserir todos os hooks prologue numa unica posicao, na ordem dada
    inject_lines = []
    for (kind, desc, pattern, code) in prologue_hooks:
        # Ja tem hook logo depois do prologue?
        already = False
        for j in range(prologue_idx + 1, min(prologue_idx + 8, len(lines))):
            if MARKER in lines[j]:
                already = True
                break
        if already:
            applied.append((desc, "ja-presente"))
            continue
        inject_lines.append(f"\t{code}\n")
        applied.append((desc, "injetado"))

    if inject_lines:
        for offset, line in enumerate(inject_lines):
            lines.insert(prologue_idx + 1 + offset, line)

    return lines, applied


def main():
    if len(sys.argv) != 3:
        print("uso: patch_instrument.py <generated/ppc> <report.txt>", file=sys.stderr)
        return 2

    ppc_dir = sys.argv[1]
    report_path = sys.argv[2]

    if not os.path.isdir(ppc_dir):
        print(f"erro: {ppc_dir} nao eh um diretorio", file=sys.stderr)
        return 2

    report_lines = []
    # Estado por funcao alvo
    state = {f: {"patched": False, "hooks": [], "missing": True} for f in HOOKS}

    # Mapeia cada funcao alvo para o arquivo onde ela foi encontrada
    # (pode ser que nem todas estejam no mesmo .cpp)
    func_to_file = {}

    cpp_files = sorted(
        f for f in os.listdir(ppc_dir) if f.endswith(".cpp")
    )

    if not cpp_files:
        print(f"erro: nenhum .cpp em {ppc_dir}", file=sys.stderr)
        return 2

    files_patched_set = set()

    for fname in cpp_files:
        fpath = os.path.join(ppc_dir, fname)
        try:
            with open(fpath, "r", encoding="utf-8", errors="replace") as fh:
                original = fh.readlines()
        except OSError as e:
            print(f"aviso: nao consegui ler {fname}: {e}", file=sys.stderr)
            continue

        # Identifica quais funcoes alvo estao neste arquivo
        present = []
        for func in HOOKS:
            if find_impl_line(original, func) >= 0:
                present.append(func)

        if not present:
            continue

        lines = list(original)

        # Se o arquivo ja foi patchado antes, nao injeta header de novo,
        # mas ainda tenta aplicar hooks que faltam (idempotencia por hook).
        header_needed = not file_already_patched(lines)

        for func in present:
            lines, applied = patch_function(lines, func, HOOKS[func])
            if applied:
                state[func]["patched"] = True
                state[func]["missing"] = False
                state[func]["hooks"] = applied
                func_to_file[func] = fname

        # Se nada mudou, nao reescreve
        if lines == original and not header_needed:
            continue

        # Injeta header no topo, se necessario
        if header_needed:
            # Procura o primeiro #include para inserir depois dele
            insert_at = 0
            for i, line in enumerate(lines):
                if line.lstrip().startswith("#include"):
                    insert_at = i + 1
            lines.insert(insert_at, "\n" + HEADER_INJECT)

        try:
            with open(fpath, "w", encoding="utf-8") as fh:
                fh.writelines(lines)
            files_patched_set.add(fname)
        except OSError as e:
            print(f"erro: nao consegui escrever {fname}: {e}", file=sys.stderr)
            return 3

    # ---- Gera relatorio ----

    for func, info in state.items():
        if info["patched"]:
            report_lines.append(f"PATCHED: {func}")
            for (desc, status) in info["hooks"]:
                report_lines.append(f"HOOK:    {func} {desc} ({status})")
            report_lines.append(f"FILE:    {func} {func_to_file.get(func, '?')}")
        else:
            report_lines.append(f"MISSING: {func}")

    report_lines.append("")
    report_lines.append(f"ARQUIVOS_PATCHADOS: {len(files_patched_set)}")
    for fname in sorted(files_patched_set):
        report_lines.append(f"FILE_PATCHED: {fname}")

    try:
        with open(report_path, "w", encoding="utf-8") as fh:
            fh.write("\n".join(report_lines) + "\n")
    except OSError as e:
        print(f"erro: nao consegui escrever relatorio: {e}", file=sys.stderr)
        return 3

    # Log no stdout (vai pro log do build)
    print(f"patch_instrument: {len(files_patched_set)} arquivo(s) patchado(s)")
    for line in report_lines:
        if line.startswith(("PATCHED:", "MISSING:")):
            print(f"  {line}")

    # Codigo de saida:
    #   0 = tudo ok (todas as 4 funcoes patchadas)
    #   1 = pelo menos uma funcao faltando (o post_instrument.sh decide)
    all_ok = all(info["patched"] for info in state.values())
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
