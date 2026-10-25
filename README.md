# Oblivion Frame Fixes

Three small fixes for Oblivion (2006) that make the game smoother, in an
xOBSE plugin. Works flat and in VR.

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
3. **A faster script variable lookup.** When an OBSE expression reads a
   variable of another script ("SomeQuest.someVariable"), xOBSE searches that
   script's variable list from the start, every single time. Mods with one big
   quest script pay for that: Maskar's Oblivion Overhaul's has 3532 variables,
   and when it builds its world events on leaving a house the game froze for
   130 ms. The plugin remembers where each variable of a long list sits and
   answers by number. Same variables, found faster.

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

- `[Main] PreciseFrameClock=1`, `FastStreamCopy=1` and `FastVariableLookup=1`
  turn the three fixes on; `0` leaves that part of the game as it is.
- `[Diagnostics] FrameClockTrace=1` writes a line every ten seconds with the
  frame lengths the game's own clock produced and the lengths the frames were
  given. Good for seeing the problem on your machine; off for play.
- `[Diagnostics] StreamCopyCheck=1` does every vertex copy twice, the game's
  way and the fast way, and compares them byte for byte. Slow; only for
  checking.
- `[Diagnostics] VariableLookupCheck=1` looks up every variable both ways,
  uses xOBSE's answer and logs any difference. Slow; only for checking.

`OblivionFrameFixes.log` in the game folder says what the plugin did.

## Heads up

- Oblivion 1.2.416 only. The fixes patch spots in the game's code; the
  plugin checks the bytes there first and does nothing if they are not the
  game's own - another game version, or another plugin that patched the same
  spot. The log says which.
- The variable lookup also patches one spot in xOBSE itself and works with
  xOBSE 22.13 only. With another xOBSE version the plugin finds other bytes
  there and leaves xOBSE's search as it is. The real fix belongs into xOBSE;
  until a version has it, this is the stopgap.
- [OBVR](https://github.com/Naragorn/OBVR), the VR mod, carries the first two
  fixes itself, not the third. With OBVR installed you only need this plugin
  for the variable lookup; for the other two, whichever loads second finds
  the spot patched and steps aside, which is harmless.
- Oblivion Display Tweaks and Oblivion Stutter Remover replace GetTickCount
  for the whole game. This plugin changes only the frame's length, one level
  below that, so the two can be used together; the plugin's precise length
  wins for the frame, and theirs still serves the game's other timers.
- Measured so far in VR under Proton with DXVK, in the people-heavy opening
  scene: the scene render a fifth to a quarter faster with the fast copy
  (3.22 ms before, 2.37 to 2.57 ms with it and the dxvk.conf line); the engine's clock
  reading 13 or 14 ms at 75 Hz before the fix and 13.33 after; and with
  `StreamCopyCheck=1` 522,000 vertex copies compared with the game's own,
  0 differences (2026-10-23, this DLL beside OBVR, which stepped aside at
  both spots as described above). With one world render a frame, as on a
  monitor, and five people in view the frame's work fell from 5.43 to
  5.06 ms, the copy itself from about 1.2 to 0.7 ms. The Windows tick is
  still to be measured; `FrameClockTrace=1` shows it on your machine.
  Display Tweaks was loaded in the test game throughout, without a conflict.
- The variable lookup, measured 2026-10-25 under Proton with Maskar's
  Oblivion Overhaul, leaving a house and walking onto new ground, everything
  else the same: MOO's world event build on leaving the house 127.6 ms
  without the fix, 30.4 ms with it; the longest frame there 164 ms without,
  76 ms with (and those 76 ms are the first draw of the new view, not the
  scripts). A frame where MOO generated one of its dungeons: 117.6 ms of
  scripts without, 55.7 ms with (not the same dungeon - MOO places them at random - but about as many script runs, 3876 and 4037). With `VariableLookupCheck=1` 800,000 lookups
  compared with xOBSE's own, 0 differences. The rest of the time scripts
  cost about 0.1 ms a frame either way.

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
