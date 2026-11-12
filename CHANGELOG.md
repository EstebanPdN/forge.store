# Changelog

## Catalog, loading and Forge Mode — 2026.11.12.3

- Expand the curated catalog to 39 entries with official icons, verified download plans and
  19 new paired screenshots. Link Butterscotch v1.3 and credit my contributions.
- Keep Red Viper Color in both Forge and Emulators; leave unreleased projects visibly in development.
- Bundle 152 public GitHub responses and bake 225 README images/avatars, add a bounded decoded SD cache, split gallery thumbnails
  from full-size images and keep the current gallery warm across video/GitHub navigation.
- Tap a lower screenshot to open it full screen; B returns. Add horizontal touch dragging to tabs.
- Bound banner movement, adjust the two Zelda backgrounds and use the actual Mario Kart 64 icon.
- Add a persistent personal-code unlock, authenticated encrypted metadata and verified
  transactional personal-file installation. Preserve existing files and exclude them from uninstall.
- Redesign the unlock panel with a correctly perpendicular hammer head and a pixel-aligned
  anvil animation that morphs into a check. Keep typed code visible and the empty caret at the start.
- Upgrade 14 official icons, add six original logos and theme 26 generated banners.
  Preserve source transparency in both themes and keep foreground artwork still at rest.
- Include platform origins and initial stars; label unpublished progress as Unknown.
- Center the 18 px Forge Mode mark. Enlarge the About coin to 40 px, remove its contact
  shadow and move the information block six pixels right.

## Audio, gallery motion and update confirmation — 2026-11-12

- Amplify music PCM by 1.60×, intro audio by 1.50× and effects by 3×, independently of the
  existing DSP mix ceiling. Check every bundled PCM file for saturation.
- Slide both gallery images for 340 ms after their textures are ready; animate thumbnail
  scrolling and use the same motion in web V2.
- Slide one continuous thumbnail selection frame with the tabs' 340 ms easing, including
  interruption and reversal, instead of switching individual thumbnail borders.
- Center the startup update content. One native pixel icon family morphs from a rotating
  stroke to a download arrow or check, animates the arrow during download, and ends in the
  same check. Three-pixel strokes stay crisp without repeating the intro logo.
- Show the target version and Yes/No before downloading an update. Rejecting it fades both
  screens to black and closes the store. Always check on launch; retain the five-second deadline.
- Make Yes/No buttons 88 pixels wide and center their group on the touch screen.
- Use the complete shared mute icon in the video button instead of an offset diagonal overlay.
- Keep the approved 90% opacity of Home information surfaces.

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
