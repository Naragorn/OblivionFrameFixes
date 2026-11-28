# State-change audit (2026-11-28)

A code read against the rule set "State changes without glitches or side effects" (claude-md-state-changes.md): every state change happens exactly as specified. Oblivion Frame Fixes has no UI. Only one fix changes what the player sees, the precise frame clock; the vertex copy, the variable lookup and the code protection give the same results faster. Nothing was built or run.

| # | Transition | Rule | What happens | Evidence | Label | Suggested fix |
|---|---|---|---|---|---|---|
| 1 | The game clock resumes after a frozen stretch, or after `iFPSClamp` stops fixing the step | Happens exactly as specified | The comment says the previous counter reading moves only when a delta was taken, so a frozen stretch reads afterwards as one long frame held to the ceiling. The condition `self->frameSeconds == given` is always true (when the precise clock does not apply, `given` is the engine's value just read), so the reading moves every frame and the first frame after the stretch gets one frame's length. The behaviour may be the better one (no jump after a pause), but code and comment disagree, and no test pins either. | `src/FrameClock.cpp:57-80`; `src/FrameClockLogic.h:52-59` | read | Decide which is wanted, make the comment and the code say the same, and add the frozen-to-running flow to the frame clock tests. |

Mapped to the rule file's principles: 1 P3 (the transition from frozen to running is not stated once and tested).

Not verified: nothing was run. `[Diagnostics] FrameClockTrace=1` around a menu pause would show which length the first frame after it gets. None of the rule file's mandatory checks were made: no stress run (low frame rate, reload, key spam, abort midway) and no per-frame state log or frame-by-frame test; until they are, every finding stays open.
