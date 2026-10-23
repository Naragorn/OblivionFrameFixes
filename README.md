# Oblivion Frame Fixes

Two small fixes for Oblivion (2006) that make every frame a little smoother,
in an xOBSE plugin. Works flat and in VR.

1. **A precise frame clock.** Oblivion measures how long a frame took with
   GetTickCount, in whole milliseconds. A 60 Hz frame of 16.67 ms reads as 16
   or 17, a 144 Hz frame of 6.94 ms as 6 or 7, so every animation and every
   moving thing speeds up and slows down by that much, frame by frame. On
   Windows the tick itself can step by 16 ms when nothing has raised the timer
   resolution - the "64 Hz bug" Oblivion Stutter Remover and Oblivion Display
   Tweaks fix in their way. This plugin gives the frame its real length from the
   high resolution counter instead, inside the game's own clock code.
2. **A faster vertex copy.** Every person in view is copied into the game's
   vertex buffers every frame, one memcpy call per vertex, 8 or 12 bytes each.
   With a few people around that was a fifth of the time a frame takes to
   draw. The plugin replaces that copy with one that does the same thing
   without the call per vertex. Same bytes, same places; the game cannot tell
   the difference, only the clock can.

**Like it? Buy me a coffee on [Ko-fi](https://ko-fi.com/somadb).** (somadb is a
different project I work on, but I confirm this Ko-fi link goes to me, the one
who made this.)

## Install

Needs [xOBSE](https://github.com/llde/xOBSE). Drop `OblivionFrameFixes.dll`
and `OblivionFrameFixes.ini` into `Data\OBSE\Plugins\`. To remove it, delete
them.

If you use DXVK, one more line in your `dxvk.conf` next to `Oblivion.exe`
helps on top:

```
d3d9.cachedWriteOnlyBuffers = True
```

The game writes its vertex buffers through a copy, and DXVK may put those
buffers into video memory, where that copy runs at PCIe speed: with people in
view that cost up to a millisecond a frame on the test machine. The line keeps
them in system memory.

## Settings

`OblivionFrameFixes.ini` next to the DLL:

- `[Main] PreciseFrameClock=1` and `FastStreamCopy=1` turn the two fixes on;
  `0` leaves that part of the game as it is.
- `[Diagnostics] FrameClockTrace=1` writes a line every ten seconds with the
  frame lengths the game's own clock produced and the lengths the frames were
  given. Good for seeing the problem on your machine; off for play.
- `[Diagnostics] StreamCopyCheck=1` does every vertex copy twice, the game's
  way and the fast way, and compares them byte for byte. Slow; only for
  checking.

`OblivionFrameFixes.log` in the game folder says what the plugin did.

## Heads up

- Oblivion 1.2.416 only. Both fixes patch two spots in the game's code; the
  plugin checks the bytes there first and does nothing if they are not the
  game's own - another game version, or another plugin that patched the same
  spot. The log says which.
- [OBVR](https://github.com/Naragorn/OBVR), the VR mod, carries both fixes
  itself. With OBVR installed this plugin is not needed; if both are there,
  whichever loads second finds the spot patched and steps aside, which is
  harmless.
- Oblivion Display Tweaks and Oblivion Stutter Remover replace GetTickCount
  for the whole game. This plugin changes only the frame's length, one level
  below that, so the two can be used together; the plugin's precise length
  wins for the frame, and theirs still serves the game's other timers.
- Measured so far in VR under Proton with DXVK (the people-heavy opening
  scene: the frame's scene render 20 % faster with the fast copy, the engine's
  clock reading 13 or 14 ms at 75 Hz before the fix and 13.33 after). Flat
  numbers and the Windows tick are still to be measured; `FrameClockTrace=1`
  shows the second one on your machine.

## Building

32-bit MSVC, CMake 3.20+:

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release .
cmake --build build
ctest --test-dir build
```

On Linux, `tools/build-linux.sh` builds the DLL with clang-cl against an xwin
winsysroot (see `cmake/toolchain-linux-clang-cl.cmake`) and runs the tests
natively.

## License

GPL-3.0, see LICENSE.
