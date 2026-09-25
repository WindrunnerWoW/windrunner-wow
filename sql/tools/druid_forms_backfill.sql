-- Nachtrag der Druiden-Gestalten fuer bereits vorhandene Bot-Charaktere.
--
-- Gehoert bewusst nach sql/tools und nicht zu den Migrationen: Das ist Nacharbeit
-- an den Charakterdaten dieses Realms, keine Aenderung, die anderswo automatisch
-- laufen soll. Neue und aufgefrischte Bots bekommen die Gestalten kuenftig von
-- PlayerbotFactory::InitAvailableSpells.
--
-- Warum das noetig ist
--   Baerengestalt (5487) und Wassergestalt (1066) lehrt kein Ausbildner; sie
--   kommen aus der Quest "Body and Heart". Bots erledigen keine Quests. Der
--   Terrorbaer (9634) wird zwar von sechzehn Ausbildnern ab Stufe 40 gelehrt,
--   setzt aber die Baerengestalt voraus - deshalb kannte ihn ebenfalls niemand.
--
-- Echte Spielercharaktere bleiben aussen vor. Fuer die ist die Quest der
-- vorgesehene Weg, und es sind ohnehin nur zwei.
--
-- Wirksam wird es beim naechsten Anmelden der Bots, da character_spell dann
-- gelesen wird - also nach dem naechsten Neustart.

-- Baerengestalt ab Stufe 10
INSERT IGNORE INTO `character_spell` (`guid`, `spell`, `active`, `disabled`)
SELECT c.guid, 5487, 1, 0
FROM `characters` c
JOIN `turtle_logon`.`account` a ON a.id = c.account
WHERE c.class = 11 AND c.level >= 10 AND a.username LIKE 'RNDBOT%';

-- Wassergestalt ab Stufe 16
INSERT IGNORE INTO `character_spell` (`guid`, `spell`, `active`, `disabled`)
SELECT c.guid, 1066, 1, 0
FROM `characters` c
JOIN `turtle_logon`.`account` a ON a.id = c.account
WHERE c.class = 11 AND c.level >= 16 AND a.username LIKE 'RNDBOT%';

-- Terrorbaer ab Stufe 40
INSERT IGNORE INTO `character_spell` (`guid`, `spell`, `active`, `disabled`)
SELECT c.guid, 9634, 1, 0
FROM `characters` c
JOIN `turtle_logon`.`account` a ON a.id = c.account
WHERE c.class = 11 AND c.level >= 40 AND a.username LIKE 'RNDBOT%';
