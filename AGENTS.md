# Working in forge.store

- The native 3DS application is the active product. Do not continue web prototype development or browser validation unless I explicitly request it.

- Preserve approved web V2 appearance and the native 400×240 / 320×240 layout. Do not edit `versions/v1/`.
- Read `docs/ARCHITECTURE.md`, `docs/BUILDING.md` and the affected original source before edits.
- Before HOME banner work, read `docs/HOME-BANNER-GUIDE.md`; verify the final CIA icon flags and embedded banner, and never equate source previews with HOME validation.
- Keep the existing C/libctru/Citro2D toolchain and stable source paths. Use shared components.
- Build with `zsh 3ds/build.sh`; avoid `--pack` unless intentionally regenerating all runtime assets.
- Test data integrity, queue/thread lifetimes and decoder behavior with the production-source host harnesses.
- Report compilation, host tests, emulator evidence and physical-console evidence separately.
- Never publish local diagnostics, credentials, ROMs, firmware, saves or private repository content.
- Keep generated intermediates in `3ds/build/` and private evidence in `.local/`, not Downloads.
- Do not create GitHub releases or deploy the website without an explicit request. Source/binary
  publication is also an explicit action; this file does not authorize it on its own.
- Use Code Atlas preview tools for browser checks. Do not replace the user's browser session.
- Public-facing copy is written in my voice. Follow the existing public port READMEs; never refer to me as “the author” or “the owner”. Reuse the established AI-development disclosure verbatim and preserve `AI_DEVELOPMENT.md`.
