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

Maps can opt out of using bridge deck to cross `lavaworld` terrain by adding
`bridgesoverrideimpassableterrain=0;` to their OTA `[GlobalHeader]`. The property
defaults to `1`, so existing maps need no changes and ordinary water bridges are
unaffected. A ground unit whose footprint center is on bridge deck is supported
across its complete footprint, so even-sized units do not sample lava through a
deck edge or the boundary between bridge sections. Bridge unit definitions
containing at least one `=` cell are also
excluded from the map's acid-water damage call: the damaging liquid remains below
the deck and no longer destroys the bridge during construction. Conventional
ground units supported by active bridge deck are excluded from that environmental
damage as well; ships passing underneath remain exposed to the liquid.

When an amphibious ground unit can traverse both routes, ordinary water immediately
beside active bridge deck receives TA's passable edge/slow path cost. This breaks
the otherwise equal-cost tie in favor of the bridge without blocking the water or
changing water elsewhere on the map. Naval movement remains unchanged.

The generic defaults are `No`; this test build enables both settings in
`res/patches.ini`. Every player in a multiplayer game must use the same settings and
unit data. See `BUILDABLE_BRIDGES.md` for the reverse-engineering evidence, exact
hook scope, compatibility height rule, and test plan.


