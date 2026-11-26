# Changelog

## forge.store 1.4.4

- Pressing HOME no longer freezes the console.
- Downloads keep going with the lid closed.
- Large ROM sets show as a single "Download ROMs" row.
- New icons and banners for Celeste 3DS and Silent Hill.

## forge.store 1.4.3

- Find Celeste 3DS and Silent Hill in Community. Silent Hill requires a New Nintendo 3DS.
- Downloads now shows when the store is checking resources already saved on your SD card.
- Update from the latest stable GitHub release. The store downloads the launcher for that version after you confirm.

## forge.store 1.4.2

- Progress shows the real download, with no long pauses.
- CIAs install while they download, like FBI.
- The game folder and its files come first, the CIA last.
- Files already on your SD card are not downloaded again.
- Uninstall only the app, or the app and its files.
- Open always starts an installed game.
- Super Mario 64 DS installs the current beta-01.
- Store updates now always show up.
- Downloads keep going with the lid closed. The green light blinks when they finish.
- My betas show one purple hammer. No more "Uses your ROM" tag.
- Old 3DS shows when a game needs a New 3DS.
- The intro shows the store version.

## forge.store 1.4

- Download the public betas of Worms 3DS, DuskLight (Twilight Princess), Red Viper Color and Super Mario 64 DS from the store. A teal gear marks each beta on its full-color icon. The orange update indicator stays visible when an installed beta has an update.
- The galleries for Worms 3DS and DuskLight now include screenshots of both screens captured in Azahar.
- Each beta lists its available launchers and installation requirements. Prepare your own game data for the port you choose.

## forge.store 1.3

- The store installs in about 11 MB instead of 50 MB. Updates are just as small.
- Fonts, the interface, every app icon and my ports' banners come inside; the rest arrives in the background after the first start, in the order you are most likely to see it: banners, galleries, Universal-DB art, GitHub pages and images, then videos. Opening an app fetches its art first.
- Art that arrives while you are looking at it fades in on its own. An update only downloads the art that changed.
- Music and sounds take 1.4 MB instead of 17 MB (Ogg Vorbis).

## forge.store 1.2

- The in-app update no longer offers the same version again after it finishes. The CIA update now installs like FBI does and only reports success once the new version is really installed.
- The Homebrew Launcher no longer lists the previous version as a second forge.store after an update, and old copies left by earlier updates are removed.
- When the .3dsx sits in /3ds/forge-store/ under a download name such as forge-store-v1.1.3dsx, it is renamed to forge-store.3dsx so the Homebrew Launcher shows its icon directly in /3ds/.
- Every port finds your ROM by its contents, under any file name and in any byte order (.z64, .v64, .n64). Star Haven also looks in the same folders the port itself searches.
- Donkey Kong 64, Banjo-Kazooie, Mystical Ninja Goemon and Mario Kart 64 only open one exact file name: the store now copies your ROM there for you, as .z64, and keeps your original.
- OpenLara and Butterscotch no longer say their game files are missing when they are on the SD card, and the SD card view shows each file where it was found.

## forge.store 1.1

- An interrupted or failed installation no longer blocks the next one ("Install recovery files exist or SD unavailable"). After a power cut, a crash or closing the store mid-install, the store settles what was left behind at launch and before every installation, so installing again works right away without restarting the store: unfinished work is undone and the previous version stays; an installation that was already complete is finished.
- Every installation writes a recovery inventory before it touches an existing file, including its CIA title and the folders it created. Anything that cannot be settled safely is set aside, never deleted.
- Apps installed with FBI, Universal-Updater or copied by hand are recognized by their CIA title, their .3dsx and other files, and Universal-Updater's own record. They show **Installed**, or **Update** when Universal-Updater installed an older version or the installed CIA is older than the current release.
- A CIA installed without the store can be updated from it when the title belongs to that app.
- Donkey Kong 64, Banjo-Kazooie and Mystical Ninja Goemon downloaded files their rolling releases no longer have (404). The catalog now follows their current files by itself.
- The catalog sync also keeps each release's CIA title and version and the Universal-DB data used to recognize installed apps.

## forge.store 1.0

- First release.
