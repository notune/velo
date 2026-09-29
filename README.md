# Velo

A fast, friendly non-linear video editor for Linux, Windows and macOS,
built with **C++20, Qt 6
and FFmpeg**. Velo follows the familiar Premiere-style layout — project bin,
media-preview/sequence monitors, effect controls and a multi-track timeline — while
staying small, hackable and quick.

![Velo editing the showcase project](dist/screenshot.png)

*The bundled showcase project: nested sequences, picture-in-picture with
keyframed rotation + Gaussian Blur + Vignette, titles with outlines, cross
dissolves and fades, audio waveforms with volume keyframes — generate it
yourself with `dist/demo/make_demo.sh`.*

## Features

- **Media** — import video, audio, images and SVG (drag files into the
  Project panel or `Ctrl+I`); thumbnails in the bin; double-click or drop
  onto the monitor to open it in the Media Preview; offline media is flagged
  and can be re-pointed via right-click → *Locate / Replace File…*.
- **Media Preview monitor** — keeps a dropdown of loaded media (✕ unloads),
  mini-timeline with draggable in/out brackets (`I` / `O` at the playhead);
  drag the picture into the timeline to insert just the in→out range.
  Stills show without a transport.
- **Sequences** — create manually with delivery presets (YouTube 1080p/4K,
  Cinema 4K DCI, TikTok/Reels, Instagram Square, …) or right-click a clip →
  *New Sequence from Clip*. Multiple sequences open as timeline tabs.
- **Timeline** — unlimited video/audio tracks; magnetic snapping (toggleable,
  `Alt` bypasses); drag clips between tracks; edge trimming with bracket
  cursors; razor tool; split selected (`S`) or all tracks (`Shift+S`);
  ripple delete (`Alt+Del`); copy/paste; per-track mute/hide, lock and
  rename; rubber-band selection; sequence tabs.
- **Linked A/V** — video and its audio move, trim, split and delete
  together; *Unlink* from the context menu when needed.
- **Nesting ("Chain")** — select clips → `Alt+C` collapses them into one
  nested-sequence clip; double-click it to open the nested sequence in a tab.
- **Monitor interaction** — move the selected clip directly in the Sequence
  monitor, scale with corner handles (uniform by default, `Shift` distorts),
  rotation/opacity in Effect Controls; drag any numeric label in Effect
  Controls left/right to scrub its value (`Shift` = 10×). Preview quality:
  Full, 1/2, 1/4, 1/8.
- **Keyframes everywhere** — position, scale, rotation, opacity, volume,
  track volume and every effect parameter animate via the stopwatch/diamond
  controls; smooth-step interpolation.
- **Audio** — waveforms on clips, a draggable volume line (Ctrl+click adds
  keyframes, drag dots to shape, double-click removes), per-track volume
  with sequence-time keyframes, clip speed with resampled audio or
  pitch-preserving time-stretch ("Preserve pitch" in Effect Controls ▸ Time);
  audible scrubbing while dragging the playhead (Playback ▸ Audio Scrubbing).
- **Effects (modular)** — Gaussian Blur, Color Correction (brightness /
  contrast / saturation / temperature), Color Grade (exposure, contrast,
  highlights/shadows, whites/blacks, white balance, hue, saturation,
  vibrance, lift/gamma/gain), Sharpen, Black & White, Vignette, Flip, audio
  Gain; drag onto clips or double-click to apply; reorder-safe parameter UI
  is generated from the effect description.
- **Transitions** — Cross Dissolve, Fade, Dip to Black; drag onto a clip
  edge or use the context menu; adjust the length by dragging the wedge in
  the timeline or in Effect Controls.
- **Titles** — `T` adds a text clip (font, size, bold/italic, fill colour,
  outline); text clips are normal clips: move, trim, animate, effect.
- **Export** — H.264/H.265 (x264/x265), NVENC hardware encoding when
  available, VP9, AV1 (SVT), ProRes, or audio-only; CRF or bitrate; any
  resolution/fps; renders through a piped `ffmpeg` process with progress
  and cancel.
- **Projects** — portable JSON `.velo` files; full snapshot undo/redo
  (`Ctrl+Z` / `Ctrl+Shift+Z`); unsaved-changes guard.
- **Custom keybindings** — every action is rebindable in *Edit ▸ Keyboard
  Shortcuts*; stored per-user.

## Download (no installation required)

Prebuilt, fully self-contained bundles for **Linux, Windows and macOS** are
on the [releases page](https://github.com/notune/velo/releases/latest) —
Qt, the FFmpeg libraries **and the `ffmpeg` export binary** are bundled at
the exact versions Velo is developed against, so nothing else needs to be
installed:

- **Linux** — `Velo-<ver>-x86_64.AppImage`: `chmod +x` and run (on
  systems without FUSE add `--appimage-extract-and-run`); or the
  `-portable.tar.xz`: extract anywhere and run `./AppRun`.
  Needs glibc ≥ 2.35 (Ubuntu 22.04+, Debian 12+, Fedora 36+).
- **Windows** — `Velo-<ver>-windows-x86_64.zip`: extract, run `velo.exe`.
- **macOS (Apple Silicon)** — `Velo-<ver>-macos-arm64.dmg`: drag Velo to
  Applications. The build is unsigned, so on first launch right-click the
  app → *Open* (or `xattr -d com.apple.quarantine /Applications/Velo.app`).

Every bundle is smoke-tested in CI (decode → composite → mix → export) on
its target platform before a release is published. Versions are pinned in
`.github/workflows/build.yml`; the app version comes from `CMakeLists.txt`
and releases are cut by pushing the matching `v<version>` tag.

## Building

Dependencies (Arch/CachyOS package names):

```
pacman -S qt6-base qt6-multimedia qt6-svg ffmpeg cmake ninja gcc pkgconf
```

```
cmake -B build -G Ninja
ninja -C build
./build/velo            # optionally: ./build/velo project.velo
```

Install system-wide (binary, .desktop entry, icon):

```
sudo cmake --install build --prefix /usr
```

A headless engine smoke test (decode → composite → mix → save/load →
export) is built in:

```
QT_QPA_PLATFORM=offscreen ./build/velo --selftest
```

## Default shortcuts

| Key | Action |
|-----|--------|
| `Space` | Play / pause |
| `←` / `→` | Step one frame (`Shift` = 5 frames) |
| `Home` / `End` | Go to start / end |
| `S` / `Shift+S` | Split selection / all tracks at playhead |
| `Del` / `Alt+Del` | Delete / ripple delete |
| `Alt+C` | Chain selection into a nested sequence |
| `V` / `C` | Selection / razor tool |
| `N` | Toggle magnetic snapping |
| `T` | Add text at playhead |
| `=` / `-` / `\` | Zoom in / out / fit |
| `Ctrl+I` / `Ctrl+N` / `Ctrl+M` | Import / new sequence / export |
| `Ctrl+K` | Keyboard shortcut editor |

(`Ctrl+wheel` zooms the timeline at the cursor; plain wheel scrolls.)

## Adding a new effect

Effects are self-contained descriptions in
`src/effects/Effects.cpp`. Register one and the effects browser,
drag-and-drop, Effect Controls UI, keyframing, serialization and rendering
all pick it up automatically:

```cpp
registerEffect({
    "posterize", "Posterize", "Stylize",
    {{"levels", "Levels", 2, 32, 6, 1, 0}},
    [](QImage &img, const QMap<QString, double> &p, double scale) {
        // mutate img in place; p has the keyframe-interpolated values
    }});
```

## Architecture

```
src/core      Project / Sequence / Track / Clip model, keyframes,
              JSON (de)serialization, Document (selection, undo, edit ops)
src/media     FFmpeg probe/decode (video + audio), waveform peaks,
              thumbnails, per-thread decoder caches
src/effects   modular effect registry + built-ins
src/engine    Compositor (frame rendering), AudioMixer/AudioEngine
              (sample-accurate mixing + playback), RenderWorker
              (coalescing preview thread), Exporter (ffmpeg pipe)
src/ui        main window, project bin, effects browser, monitors,
              timeline, effect controls, dialogs, dark theme
```

The model is shared between the GUI, the preview render thread, the audio
callback and the exporter behind a single recursive mutex; preview requests
coalesce so scrubbing never queues stale frames, and decoders only seek when
playback jumps. When one file is needed at two distant positions every frame
(cross dissolves, footage reused across nests) the cache opens an extra
decoder "lane" per position so both streams decode sequentially.

## License

Velo is free software, licensed under the **GNU General Public License,
version 3.0** — see [LICENSE](LICENSE).
