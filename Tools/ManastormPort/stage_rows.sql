-- Builds ms_stage_world from ms_coa_world (run with ms_coa_world as the default schema, after idset.sql and after
-- manastorm_port.py has written ms_port_spell). Stage tables are created LIKE Esteria's, so the dump matches its schema.
-- Rule: dependent rows (models, loot, text, ...) are copied only for things Esteria lacks; Esteria's rows always win.
DROP DATABASE IF EXISTS ms_stage_world;
CREATE DATABASE ms_stage_world CHARACTER SET utf8mb4;

-- ------------------------------------------------------------------------------------------------- key sets
DROP TABLE IF EXISTS ms_new_creature, ms_new_spawn, ms_new_go, ms_new_go_spawn, ms_new_reference, ms_new_spell;
CREATE TABLE ms_new_creature (entry INT UNSIGNED PRIMARY KEY)
    SELECT DISTINCT c.entry FROM ms_ids m JOIN creature_template c ON c.entry = m.id
    LEFT JOIN acore_world.creature_template e ON e.entry = m.id WHERE m.kind = 'creature' AND e.entry IS NULL;
-- segment / chaotic-link spawns Esteria lacks, plus every CoA spawn of a new creature
CREATE TABLE ms_new_spawn (guid INT UNSIGNED PRIMARY KEY)
    SELECT DISTINCT c.guid FROM creature c LEFT JOIN acore_world.creature e ON e.guid = c.guid
    WHERE e.guid IS NULL AND (c.guid IN (SELECT spawn_guid FROM manastorm_segment_spawn)
                              OR c.guid IN (SELECT spawn_guid FROM manastorm_chaotic_link_spawn)
                              OR c.id IN (SELECT entry FROM ms_new_creature));
CREATE TABLE ms_new_go (entry INT UNSIGNED PRIMARY KEY)
    SELECT DISTINCT g.entry FROM gameobject_template g LEFT JOIN acore_world.gameobject_template e ON e.entry = g.entry
    WHERE e.entry IS NULL AND (g.entry IN (SELECT id FROM ms_ids WHERE kind = 'gameobject')
                               OR g.entry IN (SELECT o.id FROM gameobject o JOIN manastorm_hub_return h ON h.gameobject_guid = o.guid));
CREATE TABLE ms_new_go_spawn (guid INT UNSIGNED PRIMARY KEY)
    SELECT DISTINCT o.guid FROM gameobject o LEFT JOIN acore_world.gameobject e ON e.guid = o.guid
    WHERE e.guid IS NULL AND (o.id IN (SELECT entry FROM ms_new_go)
                              OR o.guid IN (SELECT gameobject_guid FROM manastorm_hub_return));
CREATE TABLE ms_new_reference (entry INT UNSIGNED PRIMARY KEY)
    SELECT DISTINCT m.id AS entry FROM ms_ids m
    LEFT JOIN (SELECT DISTINCT Entry FROM acore_world.reference_loot_template) e ON e.Entry = m.id
    WHERE m.kind = 'reference' AND e.Entry IS NULL;
-- ms_port_spell holds only spells Esteria lacks (id = new_id) or clashes (new_id in the 14100001+ range)
CREATE TABLE ms_new_spell (id INT UNSIGNED PRIMARY KEY, new_id INT UNSIGNED NOT NULL) SELECT id, new_id FROM ms_port_spell;

-- ---------------------------------------------------------------------------------------- Manastorm tables
-- Whole tables, schema from CoA (Esteria has none of them). Generated per table so new CoA tables come along.
DROP PROCEDURE IF EXISTS ms_copy_manastorm;
DELIMITER //
CREATE PROCEDURE ms_copy_manastorm()
BEGIN
    DECLARE done INT DEFAULT 0;
    DECLARE t VARCHAR(64);
    DECLARE cur CURSOR FOR SELECT table_name FROM information_schema.tables
        WHERE table_schema = 'ms_coa_world' AND table_name LIKE 'manastorm\_%' AND table_type = 'BASE TABLE';
    DECLARE CONTINUE HANDLER FOR NOT FOUND SET done = 1;
    OPEN cur;
    copy_loop: LOOP
        FETCH cur INTO t;
        IF done THEN LEAVE copy_loop; END IF;
        SET @s = CONCAT('CREATE TABLE ms_stage_world.`', t, '` LIKE ms_coa_world.`', t, '`');
        PREPARE q FROM @s; EXECUTE q; DEALLOCATE PREPARE q;
        SET @s = CONCAT('INSERT INTO ms_stage_world.`', t, '` SELECT * FROM ms_coa_world.`', t, '`');
        PREPARE q FROM @s; EXECUTE q; DEALLOCATE PREPARE q;
    END LOOP;
    CLOSE cur;
END //
DELIMITER ;
CALL ms_copy_manastorm();
DROP PROCEDURE ms_copy_manastorm;

-- ------------------------------------------------------------------------------------------- shared tables
-- Every shared stage table takes Esteria's schema.
CREATE TABLE ms_stage_world.item_template LIKE acore_world.item_template;
CREATE TABLE ms_stage_world.item_template_locale LIKE acore_world.item_template_locale;
CREATE TABLE ms_stage_world.item_loot_template LIKE acore_world.item_loot_template;
CREATE TABLE ms_stage_world.reference_loot_template LIKE acore_world.reference_loot_template;
CREATE TABLE ms_stage_world.item_enchantment_template LIKE acore_world.item_enchantment_template;
CREATE TABLE ms_stage_world.page_text LIKE acore_world.page_text;
CREATE TABLE ms_stage_world.page_text_locale LIKE acore_world.page_text_locale;
CREATE TABLE ms_stage_world.creature_template LIKE acore_world.creature_template;
CREATE TABLE ms_stage_world.creature_template_locale LIKE acore_world.creature_template_locale;
CREATE TABLE ms_stage_world.creature_template_model LIKE acore_world.creature_template_model;
CREATE TABLE ms_stage_world.creature_template_addon LIKE acore_world.creature_template_addon;
CREATE TABLE ms_stage_world.creature_template_spell LIKE acore_world.creature_template_spell;
CREATE TABLE ms_stage_world.creature_template_resistance LIKE acore_world.creature_template_resistance;
CREATE TABLE ms_stage_world.creature_template_movement LIKE acore_world.creature_template_movement;
CREATE TABLE ms_stage_world.creature_model_info LIKE acore_world.creature_model_info;
CREATE TABLE ms_stage_world.creature_equip_template LIKE acore_world.creature_equip_template;
CREATE TABLE ms_stage_world.creature_text LIKE acore_world.creature_text;
CREATE TABLE ms_stage_world.creature_text_locale LIKE acore_world.creature_text_locale;
CREATE TABLE ms_stage_world.broadcast_text LIKE acore_world.broadcast_text;
CREATE TABLE ms_stage_world.broadcast_text_locale LIKE acore_world.broadcast_text_locale;
CREATE TABLE ms_stage_world.creature_loot_template LIKE acore_world.creature_loot_template;
CREATE TABLE ms_stage_world.creature_onkill_reputation LIKE acore_world.creature_onkill_reputation;
CREATE TABLE ms_stage_world.npc_vendor LIKE acore_world.npc_vendor;
CREATE TABLE ms_stage_world.conditions LIKE acore_world.conditions;
CREATE TABLE ms_stage_world.smart_scripts LIKE acore_world.smart_scripts;
CREATE TABLE ms_stage_world.creature LIKE acore_world.creature;
CREATE TABLE ms_stage_world.creature_addon LIKE acore_world.creature_addon;
CREATE TABLE ms_stage_world.gameobject_template LIKE acore_world.gameobject_template;
CREATE TABLE ms_stage_world.gameobject_template_addon LIKE acore_world.gameobject_template_addon;
CREATE TABLE ms_stage_world.gameobject_template_locale LIKE acore_world.gameobject_template_locale;
CREATE TABLE ms_stage_world.gameobject LIKE acore_world.gameobject;
CREATE TABLE ms_stage_world.gameobject_addon LIKE acore_world.gameobject_addon;
CREATE TABLE ms_stage_world.game_tele LIKE acore_world.game_tele;
CREATE TABLE ms_stage_world.spell_script_names LIKE acore_world.spell_script_names;
CREATE TABLE ms_stage_world.spell_proc LIKE acore_world.spell_proc;
CREATE TABLE ms_stage_world.spell_proc_event LIKE acore_world.spell_proc_event;
CREATE TABLE ms_stage_world.spell_bonus_data LIKE acore_world.spell_bonus_data;
CREATE TABLE ms_stage_world.spell_custom_attr LIKE acore_world.spell_custom_attr;
CREATE TABLE ms_stage_world.spell_linked_spell LIKE acore_world.spell_linked_spell;
CREATE TABLE ms_stage_world.spell_target_position LIKE acore_world.spell_target_position;
CREATE TABLE ms_stage_world.spell_cooldown_overrides LIKE acore_world.spell_cooldown_overrides;
CREATE TABLE ms_stage_world.spell_threat LIKE acore_world.spell_threat;
CREATE TABLE ms_stage_world.spell_dbc LIKE acore_world.spell_dbc;

-- items (ms_port_item: CoA items Esteria lacks, plus the 12 remapped "Monster -" clashes). The world.sql header widens
-- Esteria's description to CoA's 1024; ItemLimitCategory stays int16 there, so CoA's few larger IDs are remapped first.
ALTER TABLE ms_stage_world.item_template MODIFY description VARCHAR(1024) COLLATE utf8mb4_unicode_ci NOT NULL DEFAULT '',
    MODIFY ItemLimitCategory INT UNSIGNED NOT NULL DEFAULT 0;
INSERT INTO ms_stage_world.item_template SELECT i.* FROM item_template i JOIN ms_port_item p ON p.entry = i.entry;
UPDATE ms_stage_world.item_template t JOIN ms_port_limit r ON r.id = t.ItemLimitCategory SET t.ItemLimitCategory = r.new_id;
ALTER TABLE ms_stage_world.item_template MODIFY ItemLimitCategory SMALLINT NOT NULL DEFAULT 0;
INSERT INTO ms_stage_world.item_template_locale SELECT l.* FROM item_template_locale l JOIN ms_port_item p ON p.entry = l.ID;
INSERT INTO ms_stage_world.item_loot_template SELECT l.* FROM item_loot_template l JOIN ms_port_item p ON p.entry = l.Entry;
INSERT INTO ms_stage_world.reference_loot_template SELECT r.* FROM reference_loot_template r JOIN ms_new_reference n ON n.entry = r.Entry;
INSERT INTO ms_stage_world.item_enchantment_template SELECT DISTINCT t.* FROM item_enchantment_template t
    JOIN item_template i ON t.entry IN (i.RandomProperty, i.RandomSuffix) JOIN ms_port_item p ON p.entry = i.entry
    LEFT JOIN (SELECT DISTINCT entry FROM acore_world.item_enchantment_template) e ON e.entry = t.entry WHERE e.entry IS NULL;
-- shortcut: one page per item text (NextPageID chains are not followed); verify.sql reports any dangling page
INSERT INTO ms_stage_world.page_text SELECT DISTINCT t.* FROM page_text t JOIN item_template i ON i.PageText = t.ID
    JOIN ms_port_item p ON p.entry = i.entry LEFT JOIN acore_world.page_text e ON e.ID = t.ID WHERE e.ID IS NULL;
INSERT INTO ms_stage_world.page_text_locale SELECT l.* FROM page_text_locale l JOIN ms_stage_world.page_text t ON t.ID = l.ID;

-- creatures Esteria lacks
INSERT INTO ms_stage_world.creature_template SELECT c.* FROM creature_template c JOIN ms_new_creature n USING (entry);
INSERT INTO ms_stage_world.creature_template_locale SELECT c.* FROM creature_template_locale c JOIN ms_new_creature n ON n.entry = c.entry;
INSERT INTO ms_stage_world.creature_template_model SELECT c.* FROM creature_template_model c JOIN ms_new_creature n ON n.entry = c.CreatureID;
INSERT INTO ms_stage_world.creature_template_addon SELECT c.* FROM creature_template_addon c JOIN ms_new_creature n ON n.entry = c.entry;
INSERT INTO ms_stage_world.creature_template_spell SELECT c.* FROM creature_template_spell c JOIN ms_new_creature n ON n.entry = c.CreatureID;
INSERT INTO ms_stage_world.creature_template_resistance SELECT c.* FROM creature_template_resistance c JOIN ms_new_creature n ON n.entry = c.CreatureID;
INSERT INTO ms_stage_world.creature_template_movement SELECT c.* FROM creature_template_movement c JOIN ms_new_creature n ON n.entry = c.CreatureId;
INSERT INTO ms_stage_world.creature_model_info SELECT DISTINCT i.* FROM creature_model_info i
    JOIN ms_stage_world.creature_template_model m ON m.CreatureDisplayID = i.DisplayID
    LEFT JOIN acore_world.creature_model_info e ON e.DisplayID = i.DisplayID WHERE e.DisplayID IS NULL;
INSERT INTO ms_stage_world.creature_equip_template SELECT c.* FROM creature_equip_template c JOIN ms_new_creature n ON n.entry = c.CreatureID;
INSERT INTO ms_stage_world.creature_text SELECT c.* FROM creature_text c JOIN ms_new_creature n ON n.entry = c.CreatureID;
INSERT INTO ms_stage_world.creature_text_locale SELECT c.* FROM creature_text_locale c JOIN ms_new_creature n ON n.entry = c.CreatureID;
INSERT INTO ms_stage_world.broadcast_text SELECT DISTINCT b.* FROM broadcast_text b JOIN ms_stage_world.creature_text t ON t.BroadcastTextId = b.ID
    LEFT JOIN acore_world.broadcast_text e ON e.ID = b.ID WHERE e.ID IS NULL;
INSERT INTO ms_stage_world.broadcast_text_locale SELECT l.* FROM broadcast_text_locale l JOIN ms_stage_world.broadcast_text b ON b.ID = l.ID;
INSERT INTO ms_stage_world.creature_loot_template SELECT DISTINCT l.* FROM creature_loot_template l
    JOIN ms_stage_world.creature_template c ON c.lootid = l.Entry
    LEFT JOIN (SELECT DISTINCT Entry FROM acore_world.creature_loot_template) e ON e.Entry = l.Entry WHERE e.Entry IS NULL;
INSERT INTO ms_stage_world.creature_onkill_reputation SELECT r.* FROM creature_onkill_reputation r JOIN ms_new_creature n ON n.entry = r.creature_id;
INSERT INTO ms_stage_world.npc_vendor SELECT v.* FROM npc_vendor v JOIN ms_new_creature n ON n.entry = v.entry;
INSERT INTO ms_stage_world.conditions SELECT c.* FROM conditions c JOIN ms_new_creature n ON n.entry = c.SourceGroup
    WHERE c.SourceTypeOrReferenceId = 23;
INSERT INTO ms_stage_world.smart_scripts SELECT s.* FROM smart_scripts s JOIN ms_new_creature n ON n.entry = s.entryorguid
    WHERE s.source_type = 0;

-- spawns
INSERT INTO ms_stage_world.creature SELECT c.* FROM creature c JOIN ms_new_spawn n USING (guid);
INSERT INTO ms_stage_world.creature_addon SELECT a.* FROM creature_addon a JOIN ms_new_spawn n USING (guid);
INSERT INTO ms_stage_world.smart_scripts SELECT s.* FROM smart_scripts s JOIN ms_new_spawn n ON s.entryorguid = -CAST(n.guid AS SIGNED)
    WHERE s.source_type = 0;
INSERT INTO ms_stage_world.gameobject_template SELECT g.* FROM gameobject_template g JOIN ms_new_go n USING (entry);
INSERT INTO ms_stage_world.gameobject_template_addon SELECT g.* FROM gameobject_template_addon g JOIN ms_new_go n USING (entry);
INSERT INTO ms_stage_world.gameobject_template_locale SELECT g.* FROM gameobject_template_locale g JOIN ms_new_go n USING (entry);
INSERT INTO ms_stage_world.gameobject SELECT g.* FROM gameobject g JOIN ms_new_go_spawn n USING (guid);
INSERT INTO ms_stage_world.gameobject_addon SELECT g.* FROM gameobject_addon g JOIN ms_new_go_spawn n USING (guid);
INSERT INTO ms_stage_world.game_tele SELECT t.* FROM game_tele t
    JOIN (SELECT DISTINCT start_game_tele_id id FROM manastorm_segment WHERE start_game_tele_id) s USING (id)
    LEFT JOIN acore_world.game_tele e USING (id) WHERE e.id IS NULL;

-- spell rows for ported spells, keyed by CoA ID here and remapped below
INSERT INTO ms_stage_world.spell_script_names SELECT s.* FROM spell_script_names s JOIN ms_new_spell n ON n.id = ABS(s.spell_id);
-- the module's own spell scripts also bind to spells Esteria already has (Arugal's Curse, Hex of Jammal'an, tools)
INSERT IGNORE INTO ms_stage_world.spell_script_names SELECT s.* FROM spell_script_names s
    LEFT JOIN acore_world.spell_script_names e ON e.spell_id = s.spell_id AND e.ScriptName = s.ScriptName
    WHERE s.ScriptName LIKE 'spell\_manastorm%' AND e.spell_id IS NULL;
INSERT INTO ms_stage_world.spell_proc SELECT s.* FROM spell_proc s JOIN ms_new_spell n ON n.id = ABS(s.SpellId);
INSERT INTO ms_stage_world.spell_proc_event SELECT s.* FROM spell_proc_event s JOIN ms_new_spell n ON n.id = ABS(s.entry);
INSERT INTO ms_stage_world.spell_bonus_data SELECT s.* FROM spell_bonus_data s JOIN ms_new_spell n ON n.id = s.entry;
INSERT INTO ms_stage_world.spell_custom_attr SELECT s.* FROM spell_custom_attr s JOIN ms_new_spell n ON n.id = s.spell_id;
INSERT INTO ms_stage_world.spell_linked_spell SELECT s.* FROM spell_linked_spell s JOIN ms_new_spell n ON n.id = ABS(s.spell_trigger);
INSERT INTO ms_stage_world.spell_target_position SELECT s.* FROM spell_target_position s JOIN ms_new_spell n ON n.id = s.ID;
INSERT INTO ms_stage_world.spell_cooldown_overrides SELECT s.* FROM spell_cooldown_overrides s JOIN ms_new_spell n ON n.id = s.Id;
INSERT INTO ms_stage_world.spell_threat SELECT s.* FROM spell_threat s JOIN ms_new_spell n ON n.id = s.entry;
INSERT INTO ms_stage_world.spell_dbc SELECT s.* FROM spell_dbc s JOIN ms_new_spell n ON n.id = s.ID;

-- ------------------------------------------------------------------------------------------------- remaps
DROP TABLE IF EXISTS ms_item_remap, ms_spell_remap;
CREATE TABLE ms_item_remap (id INT UNSIGNED PRIMARY KEY, new_id INT UNSIGNED NOT NULL)
    SELECT entry AS id, new_entry AS new_id FROM ms_port_item WHERE entry <> new_entry;
CREATE TABLE ms_spell_remap (id INT UNSIGNED PRIMARY KEY, new_id INT UNSIGNED NOT NULL)
    SELECT id, new_id FROM ms_port_spell WHERE id <> new_id;

-- items; every UPDATE remaps a column once (joins compare the old value)
UPDATE ms_stage_world.item_template t JOIN ms_item_remap r ON r.id = t.entry SET t.entry = r.new_id;
UPDATE ms_stage_world.item_template_locale t JOIN ms_item_remap r ON r.id = t.ID SET t.ID = r.new_id;
UPDATE ms_stage_world.item_loot_template t JOIN ms_item_remap r ON r.id = t.Entry SET t.Entry = r.new_id;
UPDATE ms_stage_world.item_loot_template t JOIN ms_item_remap r ON r.id = t.Item SET t.Item = r.new_id WHERE t.Reference = 0;
UPDATE ms_stage_world.reference_loot_template t JOIN ms_item_remap r ON r.id = t.Item SET t.Item = r.new_id WHERE t.Reference = 0;
UPDATE ms_stage_world.creature_loot_template t JOIN ms_item_remap r ON r.id = t.Item SET t.Item = r.new_id WHERE t.Reference = 0;
UPDATE ms_stage_world.npc_vendor t JOIN ms_item_remap r ON r.id = t.item SET t.item = r.new_id;
UPDATE ms_stage_world.conditions t JOIN ms_item_remap r ON r.id = t.SourceEntry SET t.SourceEntry = r.new_id WHERE t.SourceTypeOrReferenceId = 23;
UPDATE ms_stage_world.manastorm_cache_bracket t JOIN ms_item_remap r ON r.id = t.cache_item_entry SET t.cache_item_entry = r.new_id;
UPDATE ms_stage_world.manastorm_cache_unlock_requirement t JOIN ms_item_remap r ON r.id = t.item_entry SET t.item_entry = r.new_id;
UPDATE ms_stage_world.manastorm_greedy_demon_loot_pool t JOIN ms_item_remap r ON r.id = t.item_entry SET t.item_entry = r.new_id;
UPDATE ms_stage_world.manastorm_material_cache_pool t JOIN ms_item_remap r ON r.id = t.item_entry SET t.item_entry = r.new_id;
UPDATE ms_stage_world.manastorm_potential_reward t JOIN ms_item_remap r ON r.id = t.item_entry SET t.item_entry = r.new_id;
UPDATE ms_stage_world.manastorm_reward_rule t JOIN ms_item_remap r ON r.id = t.item_entry SET t.item_entry = r.new_id;
UPDATE ms_stage_world.manastorm_treasure_keeper_rule t JOIN ms_item_remap r ON r.id = t.ability_case_item SET t.ability_case_item = r.new_id;
UPDATE ms_stage_world.manastorm_treasure_keeper_rule t JOIN ms_item_remap r ON r.id = t.talent_case_item SET t.talent_case_item = r.new_id;

-- spells (negative IDs in script/link/proc tables mean "all ranks" or "on remove": keep the sign)
UPDATE ms_stage_world.item_template t JOIN ms_spell_remap r ON r.id = t.spellid_1 SET t.spellid_1 = r.new_id;
UPDATE ms_stage_world.item_template t JOIN ms_spell_remap r ON r.id = t.spellid_2 SET t.spellid_2 = r.new_id;
UPDATE ms_stage_world.item_template t JOIN ms_spell_remap r ON r.id = t.spellid_3 SET t.spellid_3 = r.new_id;
UPDATE ms_stage_world.item_template t JOIN ms_spell_remap r ON r.id = t.spellid_4 SET t.spellid_4 = r.new_id;
UPDATE ms_stage_world.item_template t JOIN ms_spell_remap r ON r.id = t.spellid_5 SET t.spellid_5 = r.new_id;
UPDATE ms_stage_world.creature_template_spell t JOIN ms_spell_remap r ON r.id = t.Spell SET t.Spell = r.new_id;
UPDATE ms_stage_world.manastorm_affix_catalog t JOIN ms_spell_remap r ON r.id = t.root_spell_id SET t.root_spell_id = r.new_id;
UPDATE ms_stage_world.manastorm_affix_catalog t JOIN ms_spell_remap r ON r.id = t.death_trigger_spell_id SET t.death_trigger_spell_id = r.new_id;
UPDATE ms_stage_world.manastorm_affix_catalog t JOIN ms_spell_remap r ON r.id = t.secondary_death_trigger_spell_id SET t.secondary_death_trigger_spell_id = r.new_id;
UPDATE ms_stage_world.manastorm_affix_rank_spell t JOIN ms_spell_remap r ON r.id = t.spell_id SET t.spell_id = r.new_id;
UPDATE ms_stage_world.spell_script_names t JOIN ms_spell_remap r ON r.id = ABS(t.spell_id) SET t.spell_id = SIGN(t.spell_id) * r.new_id;
UPDATE ms_stage_world.spell_proc t JOIN ms_spell_remap r ON r.id = ABS(t.SpellId) SET t.SpellId = SIGN(t.SpellId) * r.new_id;
UPDATE ms_stage_world.spell_proc_event t JOIN ms_spell_remap r ON r.id = ABS(t.entry) SET t.entry = SIGN(t.entry) * r.new_id;
UPDATE ms_stage_world.spell_bonus_data t JOIN ms_spell_remap r ON r.id = t.entry SET t.entry = r.new_id;
UPDATE ms_stage_world.spell_custom_attr t JOIN ms_spell_remap r ON r.id = t.spell_id SET t.spell_id = r.new_id;
UPDATE ms_stage_world.spell_linked_spell t JOIN ms_spell_remap r ON r.id = ABS(t.spell_trigger) SET t.spell_trigger = SIGN(t.spell_trigger) * r.new_id;
UPDATE ms_stage_world.spell_linked_spell t JOIN ms_spell_remap r ON r.id = ABS(t.spell_effect) SET t.spell_effect = SIGN(t.spell_effect) * r.new_id;
UPDATE ms_stage_world.spell_target_position t JOIN ms_spell_remap r ON r.id = t.ID SET t.ID = r.new_id;
UPDATE ms_stage_world.spell_cooldown_overrides t JOIN ms_spell_remap r ON r.id = t.Id SET t.Id = r.new_id;
UPDATE ms_stage_world.spell_threat t JOIN ms_spell_remap r ON r.id = t.entry SET t.entry = r.new_id;
UPDATE ms_stage_world.spell_dbc t JOIN ms_spell_remap r ON r.id = t.ID SET t.ID = r.new_id;

-- display IDs the client stage (manastorm_client.py) moved: Ascension renumbered stock displays, so a CoA display ID
-- can name a different look in Esteria's client. Empty until the client stage has run.
CREATE TABLE IF NOT EXISTS ms_port_display (kind VARCHAR(16) NOT NULL, id INT UNSIGNED NOT NULL, new_id INT UNSIGNED NOT NULL,
    PRIMARY KEY (kind, id));
UPDATE ms_stage_world.item_template t JOIN ms_port_display d ON d.kind = 'item' AND d.id = t.displayid SET t.displayid = d.new_id;
UPDATE ms_stage_world.creature_template_model t JOIN ms_port_display d ON d.kind = 'creature' AND d.id = t.CreatureDisplayID
    SET t.CreatureDisplayID = d.new_id;
UPDATE ms_stage_world.gameobject_template t JOIN ms_port_display d ON d.kind = 'gameobject' AND d.id = t.displayId
    SET t.displayId = d.new_id;

-- Greedy Demon pool rows whose item Esteria already had keep Esteria's row (INSERT IGNORE), and the module refuses to
-- load if a row's tier differs from the item's quality: re-tier those rows, and disable ones outside uncommon..epic.
UPDATE ms_stage_world.manastorm_greedy_demon_loot_pool p JOIN acore_world.item_template i ON i.entry = p.item_entry
    SET p.quality_tier = i.Quality WHERE i.Quality BETWEEN 2 AND 4;
UPDATE ms_stage_world.manastorm_greedy_demon_loot_pool p JOIN acore_world.item_template i ON i.entry = p.item_entry
    SET p.enabled = 0 WHERE i.Quality NOT BETWEEN 2 AND 4;

-- Cogsley (the gadget-vendor companion) uses the full-size brokerbot model at scale 1; shrink him to companion size.
UPDATE ms_stage_world.creature_template_model SET DisplayScale = 0.3 WHERE CreatureID = 80919;
