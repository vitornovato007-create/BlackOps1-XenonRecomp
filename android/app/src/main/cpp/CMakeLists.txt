cmake_minimum_required(VERSION 3.22)
project(bo1runtime LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_C_FLAGS_DEBUG "-O1 -g0" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_DEBUG "-O1 -g0" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_RELEASE "-O1 -g0 -DNDEBUG" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-O1 -g0 -DNDEBUG" CACHE STRING "" FORCE)

set(BO1_XENONRECOMP_DIR "" CACHE PATH "Pasta do XenonRecomp clonado pelo pipeline")
set(BO1_GENERATED_DIR "" CACHE PATH "Pasta generated/ criada pelo pipeline")

set(HAVE_RECOMP OFF)

if(BO1_XENONRECOMP_DIR AND BO1_GENERATED_DIR AND EXISTS "${BO1_GENERATED_DIR}/ppc/ppc_recomp_shared.h")
    set(HAVE_RECOMP ON)
endif()

if(HAVE_RECOMP)
    message(STATUS "BO1: compilando COM o codigo recompilado")

    set(GEN "${BO1_GENERATED_DIR}/ppc")
    set(XU "${BO1_XENONRECOMP_DIR}/XenonUtils")
    set(TP "${BO1_XENONRECOMP_DIR}/thirdparty")

    file(GLOB RECOMP_SOURCES "${GEN}/ppc_recomp.*.cpp")
    list(APPEND RECOMP_SOURCES "${GEN}/ppc_func_mapping.cpp")

    if(EXISTS "${BO1_GENERATED_DIR}/kernel_stubs.cpp")
        list(APPEND RECOMP_SOURCES "${BO1_GENERATED_DIR}/kernel_stubs.cpp")
    endif()

    set(UTIL_SOURCES
        "${XU}/xex.cpp"
        "${XU}/image.cpp"
        "${XU}/xex_patcher.cpp"
        "${XU}/memory_mapped_file.cpp"
        "${XU}/xdbf_wrapper.cpp"
        "${XU}/disasm.cpp"
        "${TP}/fmt/src/format.cc"
        "${TP}/fmt/src/os.cc"
        "${TP}/tiny-AES-c/aes.c"
        "${TP}/libmspack/libmspack/mspack/lzxd.c"
        "${TP}/disasm/disasm.c"
        "${TP}/disasm/ppc-dis.c"
        "${TP}/xxHash/xxhash.c"
    )

    add_library(bo1runtime SHARED runtime.cpp ${RECOMP_SOURCES} ${UTIL_SOURCES})

    target_compile_definitions(bo1runtime PRIVATE BO1_HAVE_GENERATED=1)

    target_include_directories(bo1runtime PRIVATE
        "${GEN}"
        "${XU}"
        "${TP}/fmt/include"
        "${TP}/tiny-AES-c"
        "${TP}/libmspack/libmspack/mspack"
        "${TP}/disasm"
        "${TP}/xxHash"
        "${TP}"
        "${TP}/TinySHA1"
    )
else()
    message(STATUS "BO1: compilando SEM o codigo recompilado")
    add_library(bo1runtime SHARED runtime_stub.cpp)
endif()

target_compile_options(bo1runtime PRIVATE -w -ffunction-sections -fdata-sections)
target_link_libraries(bo1runtime PRIVATE log)
target_link_options(bo1runtime PRIVATE -Wl,--gc-sections)
