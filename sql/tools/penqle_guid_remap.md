# Remapped creature GUIDs from upstream migrations

Upstream reuses the same creature GUID block across several migrations. Where
ours is already occupied, the new spawns are moved up instead of overwriting
what is there. The migration file itself is left untouched — editing it would
make every future merge conflict on the same lines.

| Migration | Upstream | Here | Content |
|---|---|---|---|
| 20260802123824_world | 2590700-2590713 | 2910000-2910013 | Tower of Azora: Antonas Riftgaze and 13 Lesser Arcane Elementals |

The block 2590700-2590713 had been held since 20260530203924 by Stonehide Boars
and a Grimscale Thrasher in the Searing Gorge. Both groups now exist side by
side.

Check before the next merge: `SELECT MAX(guid) FROM creature;`
