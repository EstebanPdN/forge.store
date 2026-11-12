# Forge Banner Test

A separate empty application for quick HOME banner testing. It includes the current
`3ds/meta/banner.cgfx` and welcome sound, with no store RomFS or network requests.
START closes the application. HOME behavior remains unverified.

Build with `python3 3ds/banner-test/build.py` using the existing devkitPro toolchain.
The test title ID is `000400000F0F5F00`, distinct from forge.store.
The small distributable is in `builds/banner-test/`; it does not replace the store.
