# MW2 Android → Black Ops 1 porting audit

## Confirmed from the supplied MW2 Android source archive

- `build.sh` pins XenonRecomp commit `ddd128bcca99fe8bfbb99bea583c972351fa6ace`.
- Its patch is `patches/xenonrecomp-mw2.patch`.
- Its game flow assumes MW2 campaign/multiplayer executables, TU6/TU0 layouts,
  MW2-specific switch-table TOMLs, and generated output directories `ppc/` and
  `ppc_mp/`.
- `config/MW2.toml` contains MW2-specific function addresses and function bounds.
- Root CMake selects generated PPC directories and runtime behavior using
  `MW2_TITLE`, `MW2_VERSION`, and many MW2-specific options.
- Android Gradle sets `com.mw2.recomp`, expects multiple Kotlin sources, and
  selects campaign/multiplayer product flavors.
- The workflow expects both `default.xex` and `default_mp.xex` or a disc image.

## Must not be copied unchanged

- `config/MW2.toml`, `config/MW2MP.toml`, switch-table files, generated `ppc/`,
  `ppc_mp/`, XEX hashes, TU6 patch assumptions, MW2-specific XenonRecomp patch.
- Runtime code that dispatches on MW2 title/version or calls IW4-specific code.
- Android manifest/application identifiers and UI labels without reviewing
  Kotlin/JNI/native method names.
- Workflow inputs that require two MW2 executables.

## Next source files to audit before writing the real port

1. `patches/xenonrecomp-mw2.patch` — identify each change and whether generic.
2. `tools/genshared.py`, `tools/gen_kernel_stubs.py` — confirm input/output paths.
3. Root `CMakeLists.txt` — trace every runtime and generated-source dependency.
4. `runtime/title.h` and runtime initialization — locate MW2-specific symbols.
5. `android/app/src/main/java/com/mw2/recomp/NativeBridge.kt` and native bridge —
   map JNI names and library names before changing package identity.
6. `android/app/src/main/AndroidManifest.xml` and Gradle product flavors.
7. Workflow asset fetch step — replace MW2 two-executable assumptions with a
   private BO1 input path.
8. Only then define `config/BO1.toml` and the BO1-specific patch.

## Build gate

Do not produce an APK workflow that silently substitutes MW2-generated C++.
The first valid milestone is a successful BO1 XenonAnalyse/XenonRecomp generation.
The next is a native ARM64 build, followed by runtime initialization tests.
