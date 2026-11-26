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

## HOME Menu freeze (1.4.2 and the first 1.4.3 build): never again

- Cause: `background.c` opened a `gsp::Lcd` session on the first frame (to turn the screens off
  with the lid closed) and never closed it. That service allows very few sessions and the HOME
  Menu needs one, so pressing HOME froze an Old 3DS until it was powered off.
- Rule: open `gsp::Lcd`, `ndm:u` or any other shared system service only while it is in use, and
  close it right after. Never hold one for the whole run.
- Guard: `3ds/tests/run-background.py` runs the production `background.c` and fails if a frame
  with the lid open still holds `gsp::Lcd`. `3ds/build.sh` refuses to build without it passing,
  and CI runs it. Do not remove or weaken it.
- Why it came back: the fix was tested on the console but stayed uncommitted, and 1.4.3 was
  built from the committed code. Commit a fix as soon as it is confirmed. Before any release,
  check `git status` and `git stash list`: uncommitted or stashed source changes are fixes that
  will not ship. Ask about them, never drop them silently.
