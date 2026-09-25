-- Graveyards for the five Turtle-built dungeons that have none on their map.
--
-- NOT a migration on purpose. These rows reference WorldSafeLocs ids 960-964,
-- which a stock WorldSafeLocs.dbc does not contain - it stops at 174. Applied
-- without the DBC change the server just logs "has record for not existing
-- graveyard" and skips them, so nothing breaks, but nothing works either.
--
-- Order:
--   1. run tools/add_worldsafelocs.py, which appends ids 960-964 at each
--      instance entrance taken from areatrigger_teleport
--   2. restart the server so the new DBC is read
--   3. apply this file

-- Part two: five Turtle-built dungeons have no graveyard anywhere on their map,
-- so nothing can be inherited. tools/add_worldsafelocs_dungeons.py creates ids
-- 960-964 at each instance entrance (taken from areatrigger_teleport). Run it
-- and restart before applying these rows, or they will be skipped on load.

REPLACE INTO `game_graveyard_zone` (`id`, `ghost_zone`, `faction`) VALUES
    (960, 5723, 0),   -- Lower Karazhan Halls
    (961, 5601, 0),   -- Dragonmaw Retreat
    (961, 5634, 0),   -- Zuluhed's Terrace   (same map)
    (961,  296, 0),   -- South Seas          (same map)
    (961, 4016, 0),   -- Kamio               (same map)
    (962, 5640, 0),   -- Timbermaw Hold
    (963, 5641, 0),   -- Windhorn Canyon
    (964, 5734, 0);   -- Frostmane Hollow
