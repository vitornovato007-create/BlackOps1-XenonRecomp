cmake_minimum_required(VERSION 3.20)

project(BlackOps1XenonRecomp LANGUAGES NONE)

set(BO1_XEX
    "${CMAKE_SOURCE_DIR}/game/default.xex"
    CACHE FILEPATH "Caminho do executável original do BO1"
)

set(BO1_TOOLS_DIR
    "${CMAKE_BINARY_DIR}/xenonrecomp-tools"
)

set(BO1_SWITCH_TABLES
    "${CMAKE_SOURCE_DIR}/generated/BO1_switch_tables.toml"
)

find_program(BO1_BASH bash REQUIRED)
find_program(BO1_GIT git REQUIRED)

add_custom_target(bo1_check
    COMMAND "${BO1_BASH}" -c
        "test -f '${BO1_XEX}' || { echo 'ERRO: game/default.xex ausente'; exit 2; }"
    COMMAND "${BO1_GIT}" --version
    COMMENT "Verificando os arquivos de entrada do Black Ops 1"
    VERBATIM
)

add_custom_target(bo1_analyse
    COMMAND "${BO1_BASH}"
        "${CMAKE_SOURCE_DIR}/tools/analyse_bo1.sh"
        "${BO1_XEX}"
        "${BO1_SWITCH_TABLES}"
    DEPENDS bo1_check
    COMMENT "Executando a análise do executável do BO1"
    VERBATIM
)
