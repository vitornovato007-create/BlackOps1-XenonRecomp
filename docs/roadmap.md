# BlackOps1-XenonRecomp — Roadmap

## Objetivo

Desenvolver uma versão Android ARM64 de Call of Duty: Black Ops (2010), utilizando recompilação estática com XenonRecomp e uma arquitetura baseada no estudo do port Android de Modern Warfare 2.

## Fase 1 — Preparação da infraestrutura

* [x] Criar a estrutura inicial de pastas.
* [x] Criar os READMEs iniciais.
* [ ] Documentar o roadmap.
* [ ] Verificar as licenças dos componentes reutilizados.
* [ ] Selecionar versões fixas das ferramentas de compilação.

## Fase 2 — Recompilação

* [ ] Integrar XenonRecomp.
* [ ] Preparar a configuração específica de Black Ops.
* [ ] Gerar o código C++ a partir do executável do jogo.
* [ ] Verificar os imports e símbolos necessários.

## Fase 3 — Runtime

* [ ] Adaptar os componentes reutilizáveis do runtime de referência.
* [ ] Integrar os sistemas necessários para executar o código recompilado.
* [ ] Investigar compatibilidade gráfica, áudio e entrada.

## Fase 4 — Android ARM64

* [ ] Integrar Android NDK, CMake e Gradle.
* [ ] Preparar o aplicativo Android.
* [ ] Configurar a compilação para `arm64-v8a`.
* [ ] Gerar um APK de diagnóstico.

## Fase 5 — Testes

* [ ] Compilar o projeto no GitHub Actions.
* [ ] Instalar e testar o APK em um dispositivo Android compatível.
* [ ] Diagnosticar falhas de inicialização.
* [ ] Testar renderização, áudio, controles e estabilidade.

## Princípios do projeto

* Reutilizar componentes compatíveis, respeitando suas licenças.
* Manter separados os arquivos gerados e o código-fonte.
* Não considerar a compilação do APK como prova de que o jogo funciona.
* Registrar erros de compilação e testes para orientar as correções.
* Não incluir executáveis ou recursos proprietários do jogo no repositório público.

## Estado atual

Estrutura inicial criada. A integração do XenonRecomp e do runtime ainda não foi validada.
