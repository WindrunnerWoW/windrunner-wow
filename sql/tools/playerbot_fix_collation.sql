-- Align the playerbot module tables with the collation the core uses.
--
-- Only needed on installations created before the module SQL started stating the
-- collation, and only on MariaDB 11.5 or newer.
--
-- Why it matters
--   The module's CREATE TABLE statements used to say DEFAULT CHARSET=utf8 and
--   leave the collation to the server. Up to MariaDB 11.4 that meant
--   utf8mb3_general_ci, which is what the core tables state explicitly. From 11.5
--   the default became utf8mb3_uca1400_ai_ci, so the two no longer match.
--
--   Every join between a module table and a core table then fails with "Illegal
--   mix of collations". The one that hurts is in RandomPlayerbotFactory:
--
--       SELECT n.gender, n.name, e.guid
--       FROM ai_playerbot_names n LEFT OUTER JOIN characters e ON e.name = n.name
--
--   The server sees an empty result and logs "No names found in
--   ai_playerbot_names table" - even with a hundred thousand rows in it. No names
--   means no bots get created, and the message that follows is
--   "Not enough random bot accounts available", which points somewhere else
--   entirely.
--
-- Check first:
--   SELECT TABLE_NAME, TABLE_COLLATION FROM information_schema.TABLES
--   WHERE TABLE_SCHEMA IN ('tw_char','tw_world') AND TABLE_NAME LIKE 'ai_playerbot%';
--
-- Anything that is not utf8mb3_general_ci wants converting. Run the matching
-- half of this file against each database.

-- ---------------------------------------------------------------- characters
ALTER TABLE `ai_playerbot_arena_team_names` CONVERT TO CHARACTER SET utf8mb3 COLLATE utf8mb3_general_ci;
ALTER TABLE `ai_playerbot_custom_strategy`  CONVERT TO CHARACTER SET utf8mb3 COLLATE utf8mb3_general_ci;
ALTER TABLE `ai_playerbot_db_store`         CONVERT TO CHARACTER SET utf8mb3 COLLATE utf8mb3_general_ci;
ALTER TABLE `ai_playerbot_equip_cache`      CONVERT TO CHARACTER SET utf8mb3 COLLATE utf8mb3_general_ci;
ALTER TABLE `ai_playerbot_guild_names`      CONVERT TO CHARACTER SET utf8mb3 COLLATE utf8mb3_general_ci;
ALTER TABLE `ai_playerbot_item_info_cache`  CONVERT TO CHARACTER SET utf8mb3 COLLATE utf8mb3_general_ci;
ALTER TABLE `ai_playerbot_names`            CONVERT TO CHARACTER SET utf8mb3 COLLATE utf8mb3_general_ci;
ALTER TABLE `ai_playerbot_random_bots`      CONVERT TO CHARACTER SET utf8mb3 COLLATE utf8mb3_general_ci;
ALTER TABLE `ai_playerbot_rarity_cache`     CONVERT TO CHARACTER SET utf8mb3 COLLATE utf8mb3_general_ci;
ALTER TABLE `ai_playerbot_rnditem_cache`    CONVERT TO CHARACTER SET utf8mb3 COLLATE utf8mb3_general_ci;
ALTER TABLE `ai_playerbot_tele_cache`       CONVERT TO CHARACTER SET utf8mb3 COLLATE utf8mb3_general_ci;

-- --------------------------------------------------------------------- world
-- ALTER TABLE `ai_playerbot_enchants`         CONVERT TO CHARACTER SET utf8mb3 COLLATE utf8mb3_general_ci;
-- ALTER TABLE `ai_playerbot_help_texts`       CONVERT TO CHARACTER SET utf8mb3 COLLATE utf8mb3_general_ci;
-- ALTER TABLE `ai_playerbot_named_location`   CONVERT TO CHARACTER SET utf8mb3 COLLATE utf8mb3_general_ci;
-- ALTER TABLE `ai_playerbot_rpg_races`        CONVERT TO CHARACTER SET utf8mb3 COLLATE utf8mb3_general_ci;
-- ALTER TABLE `ai_playerbot_texts`            CONVERT TO CHARACTER SET utf8mb3 COLLATE utf8mb3_general_ci;
-- ALTER TABLE `ai_playerbot_texts_chance`     CONVERT TO CHARACTER SET utf8mb3 COLLATE utf8mb3_general_ci;
-- ALTER TABLE `ai_playerbot_travelnode`       CONVERT TO CHARACTER SET utf8mb3 COLLATE utf8mb3_general_ci;
-- ALTER TABLE `ai_playerbot_travelnode_link`  CONVERT TO CHARACTER SET utf8mb3 COLLATE utf8mb3_general_ci;
-- ALTER TABLE `ai_playerbot_travelnode_path`  CONVERT TO CHARACTER SET utf8mb3 COLLATE utf8mb3_general_ci;
-- ALTER TABLE `ai_playerbot_weightscale_data` CONVERT TO CHARACTER SET utf8mb3 COLLATE utf8mb3_general_ci;
-- ALTER TABLE `ai_playerbot_weightscales`     CONVERT TO CHARACTER SET utf8mb3 COLLATE utf8mb3_general_ci;
-- ALTER TABLE `ai_playerbot_zone_level`       CONVERT TO CHARACTER SET utf8mb3 COLLATE utf8mb3_general_ci;
