# Buildable-bridges patches

## Recovered linkage

Total Annihilation 3.1's yardmap parser at `0042CFAC` recognizes `.` and the normal
open/closed/terrain cell codes, but dispatches `=` to the unknown-character path.
Unknown characters are skipped rather than counted as footprint cells, so an `=`
currently shifts the remainder of the parsed yardmap.

The parser's surviving cell values use several independent bit flags. Bit zero is
not emitted by any recognized yardmap character, but its consumers remain intact:

- `UNITS_RebuildFootPrint` sets plot flag `0x02` for a bit-zero yardmap cell.
- Footprint removal clears the same plot flag.
- Adding and removing such a footprint recalculates average-height and movement
  grids around the unit.
- The cell is non-solid unless one of the separate occupancy bits is also present.
- Placement rejects overlap between bit-zero cells and an existing plot flag.

This makes yardmap value `0x01` the only coherent surviving representation of the
removed bridge cell. The November 1997 public demo already lacks the `=` parser
case, so it cannot provide an earlier handler for byte-for-byte comparison.

Runtime testing exposed a second removed linkage. The movement-grid evaluator at
`0047DFC0` rebuilds a cell from the underlying terrain's water depth and slope, but
never reads plot flag `0x02`. Retail 1.0 has the same split: its footprint routine
sets flag `0x02`, while its corresponding movement evaluator ignores it. The
traversal consumer was therefore already absent from the first retail executable.

## Yardmap parser patch

When `BuildableBridges=Yes`, the installer validates and modifies two locations in
the in-memory copy of the supported TotalA.exe:

```text
0042CFCA  Replace the ten-byte `.` cell writer with a checked hook.
0042D1A7  Route `=` through that cell handler instead of skipping it.
```

The hook stores `0x00` for `.` and `0x01` for `=`. All other yardmap characters and
their original handlers are unchanged. `TotalA.exe` is never modified on disk.

With `BuildableBridges=No` (the default), neither address is signature-checked or
modified. The acid-water protection described below is also left untouched.

## Traversal patch

`BridgeTraversal=Yes` requires `BuildableBridges=Yes`. It validates and modifies
the following additional locations in memory:

```text
0040D7B0  Add a bridge-aware fallback to the pathfinder's packed-grid query.
0043657B  Read the bridge policy while parsing GlobalHeader.lavaworld.
0043D912  Recheck a failed moving-unit plot transition against bridge deck.
0047DFC0  Replace the movement-cell evaluator with a bridge-aware equivalent.
00486109  Route unit creation height correction through the bridge wrapper.
00486306  Route network unit creation height correction through the wrapper.
0048AFB0  Route periodic unit height correction through the wrapper.
0048BA4C  Route received/sent movement height correction through the wrapper.
```

For ordinary cells, the replacement evaluator preserves the original boundary,
blocking-feature, unit-clearance, water-depth, and slope tests. A cell carrying plot
flag `0x02` retains the feature and unit tests but is treated as a level supported
surface rather than the underlying water or lava terrain.

The path-grid query preserves the original visibility and cached-grid result. Only
when that result is blocked and the candidate footprint contains at least one
bridge cell does it recalculate the candidate. Bridge cells are treated as deck;
ordinary cells in the same footprint retain the original movement-class depth,
slope, feature, and unit tests. Supporting mixed bridge/terrain footprints is
necessary at both ends of a bridge, where a unit must straddle the shore and deck
before it can stand wholly on either. The recalculation also reproduces the
original movement grid's four perimeter checks and edge-cost result. Ordinary
water or lava therefore remains unchanged. Movement classes requiring positive
water depth retain the underlying terrain result, so ships continue beneath
bridges instead of treating the deck as navigable terrain. A bridge unit's FBI
`WaterLine` is not consulted by either path calculation—it affects model placement,
not traversal.

Route creation is not the last terrain test. When a ground unit's center crosses a
plot boundary, the local movement controller calls `CanAttachUnitToPiece` and
clamps the unit back into its previous plot if the new footprint fails the unit
definition's depth or slope limits. The hook at `0043D912` leaves successful
transitions unchanged. For a failed non-naval transition containing bridge deck,
it repeats the same footprint test with bridge plots supported and all ordinary
plots, blocking features, and other-unit occupancy preserved. This allows the
unit to execute a route over the bridge rather than merely calculate one.

The height wrapper first executes the original `UNITS_FixYPos`. When that call has
recomputed the height of a conventional moving ground unit whose center is on a
bridge cell, the wrapper uses the higher of its terrain-derived height or the map
water surface as the top plane of a deck-aligned bridge model and levels pitch and
roll. The bridge's thickness must extend downward from that origin rather than
upward. The height-dirty bit is checked before the original call so periodic no-op
calls cannot add a deck offset repeatedly. Aircraft, hovercraft, floaters, and
stationary buildings are excluded. This makes the height behavior local to the
unit-update paths instead of globally changing terrain queries used by features,
projectiles, construction tests, and UI code.

### Map-level impassable-terrain policy

The traversal patch adds one integer property to the map OTA's `[GlobalHeader]`:

```text
bridgesoverrideimpassableterrain=1;
```

The default is `1`, including when the property is absent, so existing maps retain
working bridges. On a map with `lavaworld=1`, set the property to `0` to stop bridge
deck from overriding the impassable terrain below sea level. The ordinary native
terrain result then applies. The property does not disable bridge transit over
normal water on maps where `lavaworld=0`.

TA reads the new property through its existing `TdfFile::GetInt` while parsing the
adjacent native `lavaworld` property. The hook records both values and returns the
original `lavaworld` result unchanged. This keeps the policy map-scoped and avoids
assigning a new field inside TA's fixed map-definition structure.

### Acid-water protection

When `BuildableBridges=Yes`, the installer also validates and redirects the map
water-damage call at `0048AF32`. TA normally damages any eligible unit whose Y
coordinate is at or below sea level when `waterdoesdamage` and `waterdamage` are
enabled. That includes a bridge building whose placement origin is at the deck,
even though its structure spans above the liquid.

The wrapper suppresses only this environmental damage call, and only when the
target unit definition's parsed yardmap contains at least one `0x01` bridge cell
from `=`, or when a conventional non-naval ground unit's center plot is supported
by active bridge deck. Ships and submarines travelling in the liquid beneath a
bridge are not protected. The map's impassable-terrain policy must permit the deck
before a unit standing there receives protection. Weapon damage, reclaiming,
self-destruct, and every other caller of `UNITS_MakeDamage` remain unchanged. The
bridge's own protection applies while it is under construction as well as after
completion.

With `BridgeTraversal=No`, every movement, transition, path-grid, and height hook is
left untouched, including the map-level traversal-property hook. Acid protection
remains part of `BuildableBridges=Yes`. Every multiplayer participant must use
identical values for both bridge settings, identical bridge unit data, and the same
map OTA.

## Runtime validation

The plot flag and movement-grid lifecycle are recovered directly from the executable,
but the original deck-height consumer is not present in the demo, retail 1.0, or
3.1 executables examined so far. The deck-aligned height rule is consequently a
compatibility implementation and must be validated against the bridge model and
unit data before release.

Suggested test sequence:

1. Create or select a bridge unit whose `FootprintX` by `FootprintZ` yardmap uses
   `=` for every intended deck cell. Ensure the yardmap has exactly one recognized
   character per footprint cell (whitespace is allowed and ignored). Adjacent
   sections need bridge cells on their touching edges; a `.` border leaves an
   underlying-terrain seam that land pathfinding cannot cross and over which units
   will return to terrain height.
2. Place it across ordinary impassable terrain, then across lava and water. On a
   `lavaworld=1` test map, confirm the absent/default or explicit value `1` permits
   transit, while `bridgesoverrideimpassableterrain=0` restores the native blocked
   result.
3. Test conventional ground units from multiple movement classes in both directions,
   including units larger than one plot cell.
4. Confirm builders and other structures cannot overlap the bridge cells.
5. Destroy or reclaim the bridge and confirm paths are invalidated immediately.
6. Test save/load, AI pathing, queued construction, and adjacent bridge sections.
7. Confirm units remain visually on the deck rather than the terrain below it.
8. Test hovercraft, ships, submarines, aircraft, amphibious units, and transports for
   unintended height or routing changes around a bridge.
9. On a map using `waterdoesdamage=1`, verify a bridge can be completed above acid
   and a conventional ground unit standing on its deck remains unharmed, while an
   ordinary unit entering the acid and a ship underneath the bridge still take the
   configured damage.
10. In multiplayer, use identical DLL settings, map OTA, and unit data on every
    machine.
