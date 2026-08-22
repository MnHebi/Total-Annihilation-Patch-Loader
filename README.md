Total Annihilation Patch Loader
==================

Made to replace the hex edited TotalA.exe in the Total Annihilation community patch

Final OTA/Mod package could look like this (No TotalA.exe!):
![image](https://github.com/FunkyFr3sh/Total-Annihilation-Patch-Loader/assets/8355237/e8f7b5c6-f488-413b-944a-acbff79b69a6)

## Player-color focus-halo fix

This workspace variant suppresses Total Annihilation's generic six-pixel focus halo
for the 20x20 `Color%d` GAF controls used in Skirmish/Multiplayer setup. The original
focus renderer uses hardcoded palette indices `31, 28, 24, 19, 13, 6`; its expanding
rectangles overwrite adjacent rows and create the apparent highlight/remapping defect.

All other GUI controls retain their normal focus rendering. The hook validates the
original call sites and renderer signature before installing. `TotalA.exe` is not
changed on disk.

The two call sites are independently controlled by `PlayerColorFocusPrimary` and
`PlayerColorFocusLinked` in the embedded `[Settings]` section of `res/patches.ini`.
Set both to `No` and rebuild to disable the fix completely.

## Buildable bridges (experimental)

Set `BuildableBridges=Yes` in the embedded `[Settings]` section to restore `=` as
the special bridge cell in unit yardmaps. The parser stores bridge cells as `0x01`,
which reconnects the surviving non-solid footprint flag and movement-grid update
paths while leaving `.` as `0x00`.

Set `BridgeTraversal=Yes` to make those marked cells supported movement surfaces
for path-grid evaluation and to keep conventional ground units on a bridge model
whose walkable face is aligned to its placement origin. It requires
`BuildableBridges=Yes`; either setting can be disabled independently by rebuilding
the embedded INI.

The generic defaults are `No`; this test build enables both settings in
`res/patches.ini`. Every player in a multiplayer game must use the same settings and
unit data. See `BUILDABLE_BRIDGES.md` for the reverse-engineering evidence, exact
hook scope, compatibility height rule, and test plan.


