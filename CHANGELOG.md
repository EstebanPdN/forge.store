# Changelog

## Gallery and background services — 2026-11-11

- Correct the repository name to `forge.store` and update maintained download/resource links.
- Keep the Home status capsules and information card at 90% opacity; use a gear for Settings.
- Fit complete localized Play labels, put duration beside the video title, raise gallery
  navigation and separate captions from hints. Correct the missing lower screenshot scale.
- Animate both gallery screens and retain the selected screenshot while reading the Captures tab.
- Double the existing music/effect/video mix gain within the NDSP volume ceiling.
- Add the always-on, bounded startup update check with verified CIA/3DSX staging and restart.
- Share persistent conditional HTTP caching, retry transient resets and prefetch common public
  GitHub content. Prioritize visible images and retain a bounded small-image cache on return.
- Reset GitHub document layout when changing pages. Reuse the shared background renderer during
  the startup gate to prevent the black text/corner regression seen in the first candidate.

No GitHub release is created. Self-update installation tests use mocked AM services;
physical-console update/restart qualification remains pending.

## Repository preview — 2026-11-11

Initial source publication in `EstebanPdN/forge.store`, with no GitHub release.

- Native C application and approved web V2 reference, shared catalog and reviewed runtime assets.
- Seven HandBrake-compressed native previews: 16.04 MB total instead of 100.52 MB.
- Theora/IMA ADPCM playback, independent sound buffering, precise seek and bounded shutdown.
- One visible Store resources package with automatic background downloads and verified cache.
- Correct exact-zero handling of thread completion in media, SD and Universal workers.
- Nonblocking completion of GitHub reader requests; shared final-shutdown deadline for video.
- Public builds disable memory dumps; compiler flag changes invalidate object caches.
- Maintained documentation, dependency notices, repository checks and explicit build artifacts.
- Status numerals and rounded battery centred by their raster bounds; balanced battery spacing.
- Shared pixel clearance for UI lettering, consistent word gaps, measured wrapping/ellipses,
  and regular DM Sans section names with a tighter status divider.

Earlier visual work includes shared grey tabs, rounded GitHub avatars/buttons, centered
status indicators, stationary banner logos, badges above selection, and evenly stepped
volume bars. Physical-console validation and full visual parity remain ongoing.
