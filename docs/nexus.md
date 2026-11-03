# Oblivion Frame Fixes - the Nexus page

The text for the Nexus Mods page (short and plain; the README says the rest).
Version 1.0 = plugin version 1.

## Summary (the one line under the title)

Four small engine fixes for smoother frames and shorter script hitches: a
precise frame clock, a faster vertex copy, and two speed-ups for xOBSE. One
xOBSE plugin, flat and VR.

## Description

Oblivion wastes time in a few places where it doesn't have to. This plugin
fixes four of them, right where the game does the work. Nothing about how the
game plays changes, it just gets there faster.

**What it fixes**

1. **A precise frame clock.** The game measures each frame in whole
   milliseconds, so a 60 Hz frame of 16.67 ms reads as 16 or 17, and
   everything that moves speeds up and slows down by that much, frame by
   frame. The plugin gives each frame its real length.
2. **A faster vertex copy.** Every person in view gets copied into the vertex
   buffers every frame, one tiny memcpy call per vertex. The plugin does the
   same copy without the call per vertex. Same bytes, less time.
3. **Faster script variables (xOBSE).** When a mod reads a variable of a big
   quest script, xOBSE searches the whole list every time. Maskar's Oblivion
   Overhaul's has 3532 variables. The plugin remembers where they sit.
4. **Fewer system calls (xOBSE).** For many script commands xOBSE asks
   Windows several times to unlock and relock a page of game code. With a
   big script mod that's thousands of calls right after a load. The plugin
   unlocks such a page once.

**What it did on the test machine** (under Proton with DXVK; Windows numbers
still to come - the log shows yours, see below)

- The first frame after loading a save with Maskar's Oblivion Overhaul:
  2.7 s without the fixes, 1.65 s with them.
- Leaving a house with MOO: its world event build went from 128 ms to 30 ms.
- The opening scene's people: the scene draw a fifth to a quarter faster.
- Checked against the game's own way: millions of copies and lookups, zero
  differences.

**Needs**

- xOBSE.
- Oblivion 1.2.0.416 (tested with the Steam version).
- Fixes 3 and 4 need xOBSE 22.13. With another version they simply stay off.

**Install**

Install with your mod manager, or drop `OblivionFrameFixes.dll` and
`OblivionFrameFixes.ini` into `Data\OBSE\Plugins\`. To remove it, delete
them.

Using DXVK? One extra line in `dxvk.conf` next to `Oblivion.exe` helps on top:
`d3d9.cachedWriteOnlyBuffers = True`.

**Settings** (`OblivionFrameFixes.ini`)

- Each fix has its own switch under `[Main]`, 1 on, 0 off.
- `ReportGains=1` writes what each fix saved on your machine into
  `OblivionFrameFixes.log` in the game folder, every five minutes, after
  each load and when you quit.
- The `[Diagnostics]` switches compare every fix with the game's own way.
  Slow, only for checking.

**Compatibility**

- The plugin checks the game's bytes before touching anything. If another
  mod already patched a spot, it leaves that one alone and says so in its
  log.
- **OBVR** (the VR mod) has fixes 1 and 2 built in. Use both if you like:
  whoever comes second steps aside.
- **Display Tweaks / Stutter Remover:** fine together. They fix the game's
  timer, this fixes the frame length one level below.

**Source**

GPL-3.0, on GitHub: https://github.com/Naragorn/OblivionFrameFixes

Like it? Buy me a coffee on Ko-fi: https://ko-fi.com/somadb (somadb is
another project of mine; the link goes to me).

## Files

`OblivionFrameFixes-1.0.zip` with `OBSE\Plugins\OblivionFrameFixes.dll`,
`OblivionFrameFixes.ini`, the license and the README as text -
Data-relative, so mod managers place it (`tools/package-release.sh 1.0`).

## Changelog

- 1.0 (2026-11-03): first release - the frame clock, the vertex copy, the
  variable lookup and the code protection; the gains in the log.
