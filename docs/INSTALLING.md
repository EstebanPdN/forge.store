# Installing and using forge.store

Download the CIA or 3DSX linked in the root README and compare it with `builds/3ds/SHA256SUMS`.
CIA installation requires a homebrew-enabled console and FBI. For 3DSX, place the file in
`/3ds/forge-store/` and launch it through Homebrew Launcher. Keep at least 100 MB free for
installation and store resources; games need their own additional free space.

The CIA identity is unchanged: product `CTR-H-FRGS`, unique ID `0xF0F5E`.
Settings, installed metadata, caches and temporary files live under `/3ds/forge-store/`.
Updating the app should not require deleting that directory.

## First start

Choose a theme and enter the store. With Wi-Fi available, the **Store resources** package
starts automatically. The seven compressed videos total 16,044,949 bytes. Downloads shows
one row with combined progress, verification state and errors. A completed file is playable
while other files continue downloading. Without a connection, the bundled catalog and
screenshots remain usable; failed network requests can be retried.

Downloads are sequential in a background worker, which avoids starting seven transfers at
once. Game installations take priority. Files are playable only after size and SHA-256 match.
Existing verified files are reused on later starts.

## Controls

- D-pad / touch: navigate; A: open or confirm; B: back.
- L/R: change tabs. X opens GitHub from a game's detail view.
- X/Y/START/SELECT shortcuts follow the current scene; touch buttons are always visible.
- Video: A pauses, left/right seeks, Y mutes, B returns.
- `FORGE_DEBUG=1` builds reserve SELECT for private RAM/VRAM/framebuffer diagnostics;
  the public build disables that behavior. Open SD using its touch toolbar button.

## Resource cleanup

Settings → Clean video cache stops the active player and clears the cache asynchronously.
Completion is reported only after deletion succeeds. It stays cleared during the current
session. To download again, retry the resource package in Downloads or restart the store.
Files from older resource revisions can also be removed through this cleanup.

## Game data and support

Some ports need a legally obtained user file. The store checks each port's documented file
rules and shows a found filename when accepted. Store resources contain previews, not ROMs.
If HOME/close, sound or installation fails, report model, format, exact build hash and steps.
Physical-console validation remains necessary; do not treat emulator success as proof of it.
