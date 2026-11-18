# Changelog

## forge.store 1.1

- An interrupted or failed installation no longer blocks the next one ("Install recovery files exist or SD unavailable"). After a power cut, a crash or closing the store mid-install, the store settles what was left behind at launch and before every installation, so installing again works right away without restarting the store: unfinished work is undone and the previous version stays; an installation that was already complete is finished.
- Every installation writes a recovery inventory before it touches an existing file, including its CIA title and the folders it created. Anything that cannot be settled safely is set aside, never deleted.
- Apps installed with FBI, Universal-Updater or copied by hand are recognized by their CIA title, their .3dsx and other files, and Universal-Updater's own record. They show **Installed**, or **Update** when Universal-Updater installed an older version or the installed CIA is older than the current release.
- A CIA installed without the store can be updated from it when the title belongs to that app.
- Donkey Kong 64, Banjo-Kazooie and Mystical Ninja Goemon downloaded files their rolling releases no longer have (404). The catalog now follows their current files by itself.
- The catalog sync also keeps each release's CIA title and version and the Universal-DB data used to recognize installed apps.

## forge.store 1.0

- First release.
