# EasyOffset for Quest (Beat Saber 1.40.8)

A Quest port of [EasyOffset](https://github.com/Reezonate/EasyOffset) by Reezonate, plus a new
**Auto Configure** that measures your controller offsets for you by having you play three tiny maps.

Built against beatsaber-hook 6.4.2, bs-cordl 4008.0.0, BSML 0.4.55, custom-types 0.18 and SongCore 1.1.26.
The original PC source is kept in [`Source/`](Source) for reference (its README is [`README-PC.md`](README-PC.md)).

## Install

Install `EasyOffset.qmod` with [ModsBeforeFriday](https://mbf.bsquest.xyz/) on Beat Saber **1.40.8**.
BSML, SongCore, custom-types and paperlog are downloaded automatically if missing.

Open it from the **EasyOffset** button in the main menu's mod list.

## Auto Configure

1. Press **Auto Configure** and pick your controllers: **Quest 2**, **Quest 3 / 3S** or **Quest Pro**.
2. You are put into three short maps, one after another (No Fail is forced, each starts after an 8 s countdown
   or when you press *Start now*):
   1. **Up + down** – the right hand swings up, then down; then the left hand does the same.
   2. **Inlines** – two seconds of fast inline down/up swings with both hands.
   3. **Diagonal** – a single diagonal slash with both hands.
3. The results are applied and listed per hand. **Undo** brings the old offsets back.

How it works (see `src/CalibrationSolver.cpp`):

- For every fast frame, the axis the controller rotates around is taken in controller space. In a clean swing that axis
  is the swing plane normal, and the saber should lie in the plane (zero *curve* — the same target as the PC Swing
  Benchmark auto-fix).
- Map 1 + 2 give the vertical swing plane, map 3 the diagonal plane. The saber has to lie in both, so its direction is
  the intersection of the two planes. That fixes both pitch and yaw, where a single benchmark swing only fixes one.
- The fast, wrist-driven inlines of map 2 give the wrist pivot using the PC *Position Auto* solver. Directions the data
  cannot observe (inlines say nothing about where the wrist sits along the swing axis) keep the controller default.
- The vertical plane is stored as the saber **reference**, so the Curve / Balance sliders work afterwards.
- The controller choice gives the starting offsets and the sanity limits: a result more than 45° (rotation) or
  10 cm (pivot) from the controller default is rejected and reported instead of applied. Your Z offset (saber length
  preference) is never changed.

`test/run.sh` builds a host-side test that simulates the three maps with known offsets and noisy tracking and checks
that they are recovered (direction within 1–2°, pivot within ~1 cm).

The maps are written to SongCore's custom levels folder as `EasyOffset Calibration 1–3`; you can delete them any time.

## Everything else from PC

| PC feature | Quest port |
|---|---|
| Basic / Position / Rotation modes (incl. free-hand zoom) | yes |
| Position Auto / Rotation Auto | yes |
| Swing Benchmark with Auto Fix and Set Reference | yes, with live curve / wobble / swing angle and a plane gizmo |
| Direct sliders incl. Curve / Balance | yes |
| Room Offset (world pulling, per-axis locks) | yes |
| Undo / Redo, Mirror, Reset | yes |
| Presets (PC V1 / V2 files) | yes, save / load / delete |
| Assigned button, use free hand | yes (A/X, B/Y, grip, stick click) |
| Controller type | Quest 2, Quest 3/3S, Quest Pro (and Quest 1 / Rift S) |
| 3D gizmos | simplified: pivot sphere, saber line, controller axes, benchmark plane |
| Video user guide | no |

Config: `/sdcard/ModData/com.beatgames.beatsaber/Configs/EasyOffset.json` (same field names and units as PC).
Presets: `/sdcard/ModData/com.beatgames.beatsaber/Mods/EasyOffset/Presets/` — PC presets can be copied in.

**Tracking space**: *PC (Meta Link)* (default) converts the Quest controller pose to the pose PC Beat Saber sees over
Link, so PC presets mean the same thing here (constant from
[great-hb/EasyOffsetQuest](https://github.com/great-hb/EasyOffsetQuest), MIT). *Quest native* uses the raw pose.

## Building

Needs Android NDK r27, CMake 3.22+, Ninja and [qpm](https://github.com/QuestPackageManager/QPM.CLI).
Put the NDK path in `ndkpath.txt` (no spaces), then:

```
qpm restore
./scripts/build.sh
qpm qmod manifest
qpm qmod zip --skip_build EasyOffset.qmod
```

The GitHub workflow does the same and uploads `EasyOffset.qmod` as an artifact.

## Status

The mod compiles cleanly against the 1.40.8 headers and the solver is unit tested, but it has **not been tested on a
headset yet**. Logs: `/sdcard/ModData/com.beatgames.beatsaber/logs2/EasyOffset.log`. The Quest 3/3S and Pro starting
offsets are estimates (Auto Configure measures the real values); reports with your measured results are welcome.

## Credits

- [EasyOffset](https://github.com/Reezonate/EasyOffset) by Reezonate (MIT) — everything this port is based on.
- [great-hb/EasyOffsetQuest](https://github.com/great-hb/EasyOffsetQuest) (MIT) — Meta Link basis constant.
- [cheese123113/EasyOffsetQuest](https://github.com/cheese123113/EasyOffsetQuest) — earlier port; the 1.40 view-anchor handling and the build template were used as a reference.
