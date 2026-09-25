-- Blackstone Island (Goblin starting zone, ghost_zone 5536) and Thalassian Highlands
-- (High Elf starting zone, ghost_zone 5225) were seeded with faction=0 (any team),
-- so ObjectMgr::GetClosestGraveYard's enemy-faction skip never applies to them. Any
-- bot/player of any faction dying anywhere the game falls back to these graveyards gets
-- resurrected in a zone that's meant to be exclusive to one faction and off-limits to
-- random bots entirely, which is what caused bots to pile up there. 469 = Alliance,
-- 67 = Horde (matching existing single-faction zone entries elsewhere in this table).
UPDATE `game_graveyard_zone` SET `faction` = 67 WHERE `id` = 149 AND `ghost_zone` = 5536;
UPDATE `game_graveyard_zone` SET `faction` = 469 WHERE `id` = 134 AND `ghost_zone` = 5225;
