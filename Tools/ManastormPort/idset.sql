-- Everything Manastorm names, in ms_coa_world (CoA world package + Manastorm SQL). Placeholders filled by
-- manastorm_port.py: @CPP_NUMBERS@ (integers in the module's C++), @ITEM_REMAP_START@ (first remapped item - 1),
-- @CLIENT_ID_LIMIT@ (highest ID the client's supplemental tables accept).
DROP TABLE IF EXISTS ms_ids;
CREATE TABLE ms_ids (kind VARCHAR(16) NOT NULL, id INT UNSIGNED NOT NULL, src VARCHAR(32) NOT NULL, PRIMARY KEY (kind, id, src));

-- items named by Manastorm tables
INSERT IGNORE INTO ms_ids SELECT 'item', cache_item_entry, 'cache_bracket' FROM manastorm_cache_bracket WHERE cache_item_entry;
INSERT IGNORE INTO ms_ids SELECT 'item', item_entry, 'cache_unlock_req' FROM manastorm_cache_unlock_requirement WHERE item_entry;
INSERT IGNORE INTO ms_ids SELECT 'item', item_entry, 'greedy_pool' FROM manastorm_greedy_demon_loot_pool WHERE item_entry;
INSERT IGNORE INTO ms_ids SELECT 'item', item_entry, 'material_pool' FROM manastorm_material_cache_pool WHERE item_entry;
INSERT IGNORE INTO ms_ids SELECT 'item', item_entry, 'potential_reward' FROM manastorm_potential_reward WHERE item_entry;
INSERT IGNORE INTO ms_ids SELECT 'item', item_entry, 'reward_rule' FROM manastorm_reward_rule WHERE item_entry;
INSERT IGNORE INTO ms_ids SELECT 'item', ability_case_item, 'treasure_keeper' FROM manastorm_treasure_keeper_rule WHERE ability_case_item;
INSERT IGNORE INTO ms_ids SELECT 'item', talent_case_item, 'treasure_keeper' FROM manastorm_treasure_keeper_rule WHERE talent_case_item;
INSERT IGNORE INTO ms_ids SELECT 'item', entry, 'named' FROM item_template WHERE name LIKE '%Manastorm%' OR ScriptName LIKE '%manastorm%';
INSERT IGNORE INTO ms_ids SELECT 'item', entry, 'cpp' FROM item_template WHERE entry IN (@CPP_NUMBERS@);

-- spells
INSERT IGNORE INTO ms_ids SELECT 'spell', root_spell_id, 'affix' FROM manastorm_affix_catalog WHERE root_spell_id;
INSERT IGNORE INTO ms_ids SELECT 'spell', death_trigger_spell_id, 'affix' FROM manastorm_affix_catalog WHERE death_trigger_spell_id;
INSERT IGNORE INTO ms_ids SELECT 'spell', secondary_death_trigger_spell_id, 'affix' FROM manastorm_affix_catalog WHERE secondary_death_trigger_spell_id;
INSERT IGNORE INTO ms_ids SELECT 'spell', spell_id, 'affix_rank' FROM manastorm_affix_rank_spell WHERE spell_id;
INSERT IGNORE INTO ms_ids SELECT 'spell', spell_id, 'script_name' FROM spell_script_names WHERE ScriptName LIKE '%manastorm%';
-- items that teach or cast a Manastorm-scripted spell (the Cogsley companion, 98074 -> 93418) have no table naming them
INSERT IGNORE INTO ms_ids SELECT 'item', i.entry, 'teaches_tool' FROM item_template i
    JOIN (SELECT DISTINCT id FROM ms_ids WHERE kind = 'spell' AND src = 'script_name') s
    ON s.id IN (i.spellid_1, i.spellid_2, i.spellid_3, i.spellid_4, i.spellid_5);

-- creatures
INSERT IGNORE INTO ms_ids SELECT 'creature', helper_creature_entry_1, 'affix' FROM manastorm_affix_catalog WHERE helper_creature_entry_1;
INSERT IGNORE INTO ms_ids SELECT 'creature', helper_creature_entry_2, 'affix' FROM manastorm_affix_catalog WHERE helper_creature_entry_2;
INSERT IGNORE INTO ms_ids SELECT 'creature', creature_entry, 'greedy' FROM manastorm_greedy_demon_rule WHERE creature_entry;
INSERT IGNORE INTO ms_ids SELECT 'creature', creature_entry, 'treasure_keeper' FROM manastorm_treasure_keeper_rule WHERE creature_entry;
INSERT IGNORE INTO ms_ids SELECT 'creature', objective_creature_entry, 'segment' FROM manastorm_segment WHERE objective_creature_entry;
INSERT IGNORE INTO ms_ids SELECT 'creature', runtime_objective_creature_entry, 'segment' FROM manastorm_segment WHERE runtime_objective_creature_entry;
INSERT IGNORE INTO ms_ids SELECT 'creature', id, 'private_creature' FROM manastorm_private_creature;
INSERT IGNORE INTO ms_ids SELECT 'creature', entry, 'named' FROM creature_template
    WHERE name LIKE '%Manastorm%' OR ScriptName LIKE '%manastorm%' OR name IN ('Cogsley', 'Cogsworth');
INSERT IGNORE INTO ms_ids SELECT 'creature', entry, 'cpp' FROM creature_template WHERE entry IN (@CPP_NUMBERS@) AND entry <> 100000;

-- spawns and gameobjects
INSERT IGNORE INTO ms_ids SELECT 'spawn', spawn_guid, 'segment_spawn' FROM manastorm_segment_spawn;
INSERT IGNORE INTO ms_ids SELECT 'gameobject_guid', gameobject_guid, 'hub_return' FROM manastorm_hub_return;
INSERT IGNORE INTO ms_ids SELECT 'gameobject', id, 'hub_spawn' FROM gameobject g JOIN manastorm_hub_return h ON h.gameobject_guid = g.guid;
INSERT IGNORE INTO ms_ids SELECT 'gameobject', entry, 'named' FROM gameobject_template WHERE name LIKE '%Manastorm%' OR ScriptName LIKE '%manastorm%';
INSERT IGNORE INTO ms_ids SELECT 'gameobject', entry, 'cpp' FROM gameobject_template WHERE entry IN (@CPP_NUMBERS@);

-- closure: what Manastorm NPCs sell, what caches contain (two levels of nested references)
INSERT IGNORE INTO ms_ids SELECT 'item', v.item, 'vendor' FROM npc_vendor v JOIN ms_ids m ON m.kind = 'creature' AND m.id = v.entry;
DROP TEMPORARY TABLE IF EXISTS ms_containers;
CREATE TEMPORARY TABLE ms_containers (id INT UNSIGNED PRIMARY KEY)
    SELECT DISTINCT id FROM ms_ids WHERE kind = 'item' AND src <> 'greedy_pool';
INSERT IGNORE INTO ms_ids SELECT 'item', l.Item, 'cache_loot' FROM item_loot_template l JOIN ms_containers c ON c.id = l.Entry WHERE l.Reference = 0;
INSERT IGNORE INTO ms_ids SELECT 'reference', l.Reference, 'cache_loot' FROM item_loot_template l JOIN ms_containers c ON c.id = l.Entry WHERE l.Reference <> 0;
INSERT IGNORE INTO ms_ids SELECT 'reference', r.Reference, 'nested' FROM reference_loot_template r
    JOIN (SELECT DISTINCT id FROM ms_ids WHERE kind = 'reference') m ON m.id = r.Entry WHERE r.Reference <> 0;
INSERT IGNORE INTO ms_ids SELECT 'reference', r.Reference, 'nested2' FROM reference_loot_template r
    JOIN (SELECT DISTINCT id FROM ms_ids WHERE kind = 'reference') m ON m.id = r.Entry WHERE r.Reference <> 0;
INSERT IGNORE INTO ms_ids SELECT 'item', r.Item, 'reference_loot' FROM reference_loot_template r
    JOIN (SELECT DISTINCT id FROM ms_ids WHERE kind = 'reference') m ON m.id = r.Entry WHERE r.Reference = 0;

-- item port list: CoA items Esteria lacks keep their IDs, unless above the client's table limit; an ID Esteria uses for a
-- "Monster -" placeholder that Ascension turned into a real item gets a new ID. Other same-ID pairs are the same item
-- renamed: Esteria's stays.
DROP TABLE IF EXISTS ms_port_item;
CREATE TABLE ms_port_item (entry INT UNSIGNED PRIMARY KEY, new_entry INT UNSIGNED NOT NULL UNIQUE, reason VARCHAR(16) NOT NULL);
INSERT INTO ms_port_item SELECT DISTINCT m.id, m.id, 'missing' FROM ms_ids m JOIN item_template c ON c.entry = m.id
    LEFT JOIN acore_world.item_template e ON e.entry = m.id WHERE m.kind = 'item' AND e.entry IS NULL AND m.id <= @CLIENT_ID_LIMIT@;
SET @next := @ITEM_REMAP_START@;
INSERT INTO ms_port_item SELECT x.id, (@next := @next + 1), x.reason FROM (
    SELECT DISTINCT m.id, IF(e.entry IS NULL, 'client-limit', 'remap') reason FROM ms_ids m JOIN item_template c ON c.entry = m.id
    LEFT JOIN acore_world.item_template e ON e.entry = m.id
    WHERE m.kind = 'item' AND (e.entry IS NULL AND m.id > @CLIENT_ID_LIMIT@ OR e.name <> c.name AND e.name LIKE 'Monster -%')
    ORDER BY m.id) x;
