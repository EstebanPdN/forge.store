# Store resources

The first visit to Home starts one persistent background worker. Downloads exposes that
worker's seven entries as **one package**, with byte-weighted progress. Transfers are
sequential; game installation pauses the package. The UI never needs to wait for all videos
before a completed, verified video can play.

## Published set

`assets/store-resources/report.json` records encoder versions, duration, frame count,
bitrate, quality measurements, byte length and SHA-256 for each reviewed file.
The set contains 6,010 frame records and **16,044,949 bytes**, down from 100,523,582 bytes
in the original MJPEG/PCM set (84.0% smaller). Trailers are under 6 MB each and split-screen
clips under 1.2 MB. Browser MP4 previews remain in their existing per-port asset folders.

`hosting.json` pins native resources to a Git commit in **EstebanPdN/forge.store**.
`media-manifest.py` verifies the reviewed files and writes both the public manifest and
`3ds/romfs/media.json`. URLs are anonymous HTTPS raw-file links. There are no resource
releases and no dependency on a game's latest release ordering.

## Integrity and state

- Worker-owned `.part` writes are never playable. Exact size and SHA-256 must match before
  atomic rename to a hash-named cache file.
- Existing cached files are reverified, so stale/corrupt content does not count as ready.
- Retryable failures receive bounded retries; permanent failures are shown, not retried forever.
- Opening a detail view may prioritize its video. Leaving it detaches the watcher without
  cancelling the background package.
- Settings cleanup first waits for decoder stop, then removes files in the worker and reports
  the actual result. Cleanup does not immediately trigger another automatic download.
- The next launch or an explicit retry restarts cleared entries.

Public queue APIs are documented in `3ds/source/media.h`. The package view is aggregated in
`scenes.c` and drawn in `panels.inc`. Files download separately under one visible package;
there is no large ZIP that must be fully downloaded and extracted before playback.

## Encoding and decoding

The `FVD2` container keeps the `.fvid` extension. Its version-1 binary header, setup packets,
keyframe index and frame records are documented at the top of `3ds/tools/pack-video.py`.
The video payload is Theora 4:2:0 at 20 frames/s, 400 × 240 top and optional 320 × 240 bottom.
Trailers use mono 24 kHz IMA ADPCM. Each audio record stores its own predictor/index for seeking.

FFmpeg first creates lossless native-resolution masters. HandBrake encodes Theora; FFmpeg
remuxes without another video encode. A declared 2 fps intermediate gives HandBrake a short
25-frame keyframe interval; FVD2 restores the actual 20 fps timing. The target decoder never
plays at 2 fps. Pixel clips preserve reviewed nearest-neighbor sampling; trailers use bilinear
resampling. All encoding/quality details remain in the report and generator.

The native decoder works off-thread with bounded frame/sound buffers. Sound is queued ahead
of pictures. Duplicate frames reuse existing GPU textures. Under load, it skips conversion
and may jump to a keyframe to keep the clock responsive; this is a performance tradeoff,
not a guarantee of full frame rate on physical hardware. Seeking rebuilds references from
the correct keyframe and publishes only the requested generation. Legacy FVID/MJPEG support
remains available, although the current download manifest contains only FVD2.

## Rebuilding or updating the set

```sh
3ds/tools/.venv/bin/python -I 3ds/tools/pack-video.py --force
3ds/tools/.venv/bin/python -I 3ds/tools/check-media.py --dir 3ds/media-next
3ds/tools/.venv/bin/python -I 3ds/tests/run-video-test.py --all --media 3ds/media-next
```

Output initially stays in ignored `3ds/media-next/` for review. After inspection, copy the
seven files and a path-sanitized report into `assets/store-resources/`, commit those assets,
then update `hosting.json` with that commit and their verified hashes. Rebuild the native
manifest and application. Never point the native manifest at an untested codec or change
files at a pinned URL. Publication is an explicit maintainer action; no release workflow runs.
