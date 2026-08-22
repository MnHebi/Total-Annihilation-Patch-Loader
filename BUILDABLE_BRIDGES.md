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
modified.

## Traversal patch

`BridgeTraversal=Yes` requires `BuildableBridges=Yes`. It validates and modifies
the following additional locations in memory:

```text
0040D7B0  Add a bridge-aware fallback to the pathfinder's packed-grid query.
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

With `BridgeTraversal=No`, every movement, transition, path-grid, and height hook is
left untouched. Every multiplayer participant must use identical values for both
bridge settings and identical bridge unit data.

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
2. Place it across ordinary impassable terrain, then across lava and water.
3. Test conventional ground units from multiple movement classes in both directions,
   including units larger than one plot cell.
4. Confirm builders and other structures cannot overlap the bridge cells.
5. Destroy or reclaim the bridge and confirm paths are invalidated immediately.
6. Test save/load, AI pathing, queued construction, and adjacent bridge sections.
7. Confirm units remain visually on the deck rather than the terrain below it.
8. Test hovercraft, ships, submarines, aircraft, amphibious units, and transports for
   unintended height or routing changes around a bridge.
9. In multiplayer, use identical DLL settings and unit data on every machine.
