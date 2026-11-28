# dendrite

**A dub echo that grows around your head.** VST3 (plus AU on macOS), built with JUCE. Put it on a track and play.
The repository is called `knit`; the plug-in is called **dendrite**, always lowercase (it was called viber until 0.2.0).

**Status: released (2.5).** Download it from [afferent.page](https://afferent.page) or the [GitHub releases](https://github.com/hakdiriko/knit/releases). A dotted-eighth dub echo locked to the host tempo. Every repeat steps a little further out from the middle, so the echo grows from inside your head to all around it, while the mono sum stays a clean dub echo. It breathes with your playing, and when a part repeats too long it gets an itch: three of five heads (scream, laugh, manipulate, swarm, ghost) answer. It also keeps the last minute, so you can drag an itch straight into Live.

Since 3.0 the window is the display of an electrophysiology rig, and the fly's brain is the specimen. Every number on it is measured.

| Control | What you hear |
|---|---|
| feed (the electrode, up) | how long the repeats live: off, whisper, dub, drown, endless |
| space (the electrode, right) | where they live: inside, around, envelop, beyond. Mono-safe all the way |
| throw | press the specimen anywhere away from the electrode and hold; at feed off it's the only way in |
| itch selector | how awake the fly is kept, 1/4 bar (wide awake) to 8 bars, or off (at rest) |
| keep | lines in the log marked with a square are still on tape: drag one into Live, cut on bar lines |

The manual (22 pages: every control, every reading, the fly, the kick, imaging, lights out) is [docs/manual/dendrite-manual.pdf](docs/manual/dendrite-manual.pdf), and it comes inside every download.

Made for Windows 11 and Ableton Live 12; macOS builds (VST3 and AU) are made too, but not code-signed.

## Get a build

Releases: on the Actions page, **Run workflow** on `main` with **publish** ticked (or push a tag `vX.Y.Z` matching the version in `CMakeLists.txt`). That builds Windows, macOS and Linux, validates all three with pluginval, and publishes a GitHub release with a Windows installer, zips for every system, the manual and `CREDITS.txt`.

Every push also builds the plug-in automatically. You never need to compile anything.

1. Open the [Actions tab](https://github.com/hakdiriko/knit/actions/workflows/build.yml) (you need to be signed in to GitHub).
2. Click the newest run with a green tick.
3. Scroll to **Artifacts** and download:
   - Windows: `dendrite-windows-vst3` (every push)
   - macOS: `dendrite-macos-vst3-au` (only on runs started by hand: **Run workflow** on the Actions page)

## Install in Ableton Live on Windows

1. **Quit Live.** Windows locks a loaded plug-in file, so replacing it while Live is open fails or leaves the old version running.
2. Unzip `dendrite-windows-vst3.zip`. Inside are a folder called `dendrite.vst3` and the manual, `dendrite-manual.pdf`.
3. Copy the whole `dendrite.vst3` folder into `C:\Program Files\Common Files\VST3\`. Windows asks for administrator permission; allow it. If an older `dendrite.vst3` or `Dendrite.vst3` is there, replace it. **If a `viber.vst3` is there, delete it**: it is the same plug-in under its old name, and two copies confuse Live.
4. Start Live and open **Options → Settings → Plug-Ins**.
5. Turn on **Use VST3 Plug-In System Folders** (once only).
6. Click **Rescan**. If dendrite doesn't appear, hold **Alt** while clicking Rescan to force a full rescan.
7. In the browser, open **Plug-Ins → VST3 → afferent → dendrite** and drag it onto a track.

## Install in Ableton Live on macOS

1. Quit Live and unzip `dendrite-macos-vst3-au.zip`, then the two zips inside it.
2. Copy `dendrite.vst3` to `~/Library/Audio/Plug-Ins/VST3/` and `dendrite.component` to `~/Library/Audio/Plug-Ins/Components/` (replace any `Dendrite` versions, and delete any old `viber.vst3` and `viber.component`).
3. The builds are not code-signed, so macOS quarantines them. Open Terminal and run:
   ```
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/dendrite.vst3 ~/Library/Audio/Plug-Ins/Components/dendrite.component
   ```
4. Start Live, open **Live → Settings → Plug-Ins**, turn on **Use Audio Units v2** and/or **Use VST3 Plug-In System Folders**, and click **Rescan** (hold **Option** for a full rescan).

## For developers

See [CLAUDE.md](CLAUDE.md), [docs/PROJECT_BRIEF.md](docs/PROJECT_BRIEF.md) and [docs/DSP_PLAYBOOK.md](docs/DSP_PLAYBOOK.md).
Design decisions are in [DECISIONS.md](DECISIONS.md); changes per version in [CHANGELOG.md](CHANGELOG.md).

## Rights

© 2026 afferent. All rights reserved. The source is public so it can be read and built, but no licence is granted to copy, modify or redistribute it.

The fonts in `Resources/fonts/` (Fragment Mono, Source Serif 4) are under the SIL Open Font License; JUCE is used under its own licence.

The fly's wiring comes from Janelia's male CNS connectome v1.0 (Berg et al. 2026), released under CC BY 4.0. Full credits: [packaging/CREDITS.txt](packaging/CREDITS.txt). Until 3.0 the fly came from the FlyWire connectome (CC BY-NC 4.0, non-commercial); it can still be built with `-DDENDRITE_FLY=flywire` (credits: [packaging/CREDITS-flywire.txt](packaging/CREDITS-flywire.txt)) but is not for sale without FlyWire's permission.
