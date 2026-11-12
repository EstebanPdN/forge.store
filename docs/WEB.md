# Web design reference

Run `python3 tools/serve.py 8642` from the repository root and open `http://localhost:8642`.
The server supports byte ranges for videos and a bounded GitHub API proxy/cache. If a local
GitHub CLI login or GITHUB_TOKEN exists, authentication stays in the server process and is
never embedded in the page. The server listens on loopback by default.

The web is the V2 layout reference: 400 × 240 top, 320 × 240 bottom. Use the resolution and
pixel magnifier tools to check native sizes. Eight languages, both themes, tab transitions,
icon selection and the static foreground logos are maintained here. V1 remains unchanged
in `versions/v1/`.

Its SD and installation flows are simulated in localStorage. Browser preview MP4s are in
`assets/ports/*/video/`; native FVD2 videos are separate files in `assets/store-resources/`.
The browser does not execute the native background resource queue. Use the native test
build to validate that behavior.

A static server can show the site, but the local GitHub proxy is not part of GitHub Pages.
Without it, the client falls back to public GitHub API requests and their unauthenticated
limits. No website deployment is configured by this repository publication.
