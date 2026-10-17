# BlackOps1-XenonRecomp

Android ARM64 static-recompilation research project for Call of Duty: Black Ops (2010).

## Status

This repository is in the infrastructure/porting phase. It is **not yet a playable port**.

The MW2 Android repository is used as an architectural reference. Its generated `ppc/` code, MW2-specific addresses, executable hashes, title-update assumptions, runtime title dispatch, and MW2-specific patches must not be reused as if they belonged to Black Ops.

## Required input

Provide a legally obtained Black Ops Xbox 360 executable (`default.xex`) through a private workflow input or private storage. Do not commit game executables, disc images, title updates, or proprietary game assets to this repository.

## Current bootstrap package

- `.github/workflows/android.yml`: checks repository structure and validates the toolchain prerequisites; it intentionally does not claim to build an APK before the BO1 config/runtime exists.
- `build.sh`: validates the input and stops with an explicit message until a verified BO1 XenonRecomp configuration is present.
- `config/BO1.toml.template`: documentation-only template. Addresses and jump-table data must be derived from the exact executable; do not rename this file to `BO1.toml` and expect recompilation to work.

## Next milestones

1. Analyze the exact BO1 XEX and determine the XenonRecomp version/configuration.
2. Add BO1-specific jump tables, function bounds, imports and patches.
3. Port or adapt runtime components with verified compatibility.
4. Integrate Android Gradle/JNI/CMake and build an installable diagnostic APK.
5. Test runtime initialization, rendering, audio and input on device.

Do not treat a successful infrastructure check as proof that an APK or playable game exists.
