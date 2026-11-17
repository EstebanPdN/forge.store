# Installing and using forge.store

## Requirements

- An Old or New Nintendo 3DS, 3DS XL, 2DS or 2DS XL with custom firmware (Luma3DS).
- FBI for the CIA, or the Homebrew Launcher for the 3DSX.
- At least 100 MB free on the SD card for the store and its resources. Each game needs its
  own additional space.
- Wi-Fi for downloads, updates and GitHub pages. The bundled catalog works offline.

## Install the CIA (recommended)

1. Open **FBI → Remote Install → Scan QR Code**.
2. Scan the QR code in the [README](../README.md#download-and-installation). It points to
   `builds/3ds/forge-store.cia` in this repository.
3. Confirm the installation and return to the HOME Menu. **forge.store** appears with its
   own banner.

You can also download [forge-store.cia](https://raw.githubusercontent.com/EstebanPdN/forge.store/main/builds/3ds/forge-store.cia),
check it against [SHA256SUMS](../builds/3ds/SHA256SUMS), copy it to the SD card and install it
from FBI. The CIA identity never changes: product `CTR-H-FRGS`, unique ID `0xF0F5E`.

## Install the 3DSX

Copy [forge-store.3dsx](https://raw.githubusercontent.com/EstebanPdN/forge.store/main/builds/3ds/forge-store.3dsx)
to `sdmc:/3ds/forge-store/` and open it from the Homebrew Launcher.

## First start

Choose a theme and enter the store. With Wi-Fi available, the **Store resources** package
starts automatically. Its seven compressed trailers total 16,044,949 bytes. Downloads shows
one row with combined progress, verification state and errors. Each trailer becomes playable
as soon as its own file passes its size and SHA-256 check, while the others keep downloading.

Downloads run one at a time in a background worker, and game installations take priority.
Verified files are reused on later starts. Without a connection, the bundled catalog and
screenshots remain usable and failed requests can be retried.

Settings, installed metadata, caches and temporary files live under `sdmc:/3ds/forge-store/`.
Updating the store never requires deleting that directory.

## Updating

Every launch checks for a newer store build before the introduction. When one is found,
the touch screen shows its version:

- **Yes** downloads, verifies and installs it, then restarts the store.
- **No** fades to black and closes the store.

If the check cannot reach GitHub, the store continues normally. Builds older than the
updater need one manual update first. Details are in [Updates and cache](UPDATES-AND-CACHE.md).

## Controls

| Input | Action |
| --- | --- |
| D-Pad / Touch screen | Navigate |
| A | Open / Confirm |
| B | Back |
| L / R | Change tabs; drag the category strip to reveal more |
| X | Open the GitHub page from a game’s detail view |
| Y / START / SELECT | Scene shortcuts; their touch buttons are always visible |
| Video: A | Pause / Resume |
| Video: ◀ ▶ | Seek |
| Video: Y | Mute |
| Video: B | Return |

In Captures, tap a lower-screen image to view it at 320×240; B returns to the gallery.
Open the SD browser with its touch toolbar button.

## Game files

Some ports need a legally obtained file of your own. Each detail page lists the expected
location and filename, and the store shows the file it found once it is accepted.
The Store resources package contains previews only, never ROMs or game data.

## Freeing space

**Settings → Clean video cache** stops the active player and clears the cache in the
background. It reports completion only after the files are deleted, and also removes files
from older resource revisions. To download the trailers again, retry the package in
Downloads or restart the store.

## Forge Mode

**Settings → Console** opens the secret-code keyboard. A valid personal code authenticates
the encrypted resource manifest, animates the forge into a check and saves activation
locally. The orange **forge mode enabled** indicator stays beside the console model after
restarting. The code and personal download addresses never appear in logs.

For configured ports, installation downloads missing personal files through the same
size- and hash-checked transaction. Existing files are preserved. New personal files roll
back with a failed install but remain on the SD card when the application is uninstalled.

The repository contains an authenticated encrypted manifest, not game payloads or its
decryption key. Activation stores a derived key on the local SD card (or localStorage in the
web reference). A short shared code can be guessed offline, so this is convenience
encryption, not access control for a public hosting service.

## Troubleshooting

| Problem | What to try |
| --- | --- |
| The update check is skipped | Check Wi-Fi; the store continues normally without it. |
| A trailer will not play | Open Downloads and retry the Store resources package. |
| An install fails | Free space on the SD card and try again; failed installs roll back. |
| HOME, sleep, sound or launch problems | Report them with the details below. |

When reporting a problem on [GitHub](https://github.com/EstebanPdN/forge.store/issues) or
[Discord](https://discord.gg/BjbzfRYpV), include the console model, CIA or 3DSX, when you
installed the store and the steps to reproduce it. Emulator results are useful, but
they do not replace a test on a real console.
