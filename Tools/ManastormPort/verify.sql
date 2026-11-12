-- Post-apply checks on ms_test_world (Esteria's world + the stage). Each line is "check<TAB>count"; every count
-- should be 0 except the ones marked "info".
SELECT 'item: manastorm table item missing', COUNT(*) FROM (
    SELECT cache_item_entry i FROM manastorm_cache_bracket WHERE cache_item_entry
    UNION ALL SELECT item_entry FROM manastorm_cache_unlock_requirement WHERE item_entry
    UNION ALL SELECT item_entry FROM manastorm_greedy_demon_loot_pool WHERE item_entry
    UNION ALL SELECT item_entry FROM manastorm_material_cache_pool WHERE item_entry
    UNION ALL SELECT item_entry FROM manastorm_potential_reward WHERE item_entry
    UNION ALL SELECT item_entry FROM manastorm_reward_rule WHERE item_entry
    UNION ALL SELECT ability_case_item FROM manastorm_treasure_keeper_rule WHERE ability_case_item
    UNION ALL SELECT talent_case_item FROM manastorm_treasure_keeper_rule WHERE talent_case_item) q
    LEFT JOIN item_template t ON t.entry = q.i WHERE t.entry IS NULL;
SELECT 'item: loot item missing', COUNT(*) FROM (
    SELECT Item FROM item_loot_template WHERE Reference = 0 UNION ALL SELECT Item FROM reference_loot_template WHERE Reference = 0
    UNION ALL SELECT Item FROM creature_loot_template WHERE Reference = 0) q LEFT JOIN item_template t ON t.entry = q.Item WHERE t.entry IS NULL;
SELECT 'item: loot reference missing', COUNT(*) FROM (
    SELECT Reference r FROM item_loot_template WHERE Reference <> 0 UNION ALL SELECT Reference FROM reference_loot_template WHERE Reference <> 0) q
    LEFT JOIN (SELECT DISTINCT Entry FROM reference_loot_template) r ON r.Entry = q.r WHERE r.Entry IS NULL;
SELECT 'item: vendor item missing', COUNT(*) FROM npc_vendor v LEFT JOIN item_template t ON t.entry = v.item WHERE v.item > 0 AND t.entry IS NULL;
SELECT 'info: item page text missing (4 Manastorm items, missing in CoA too)', COUNT(*) FROM item_template i LEFT JOIN page_text p ON p.ID = i.PageText WHERE i.PageText > 0 AND p.ID IS NULL;
SELECT 'info: greedy item random property/suffix group missing (missing in CoA too; core logs it, no random stats)', COUNT(*) FROM item_template i
    LEFT JOIN (SELECT DISTINCT entry FROM item_enchantment_template) e ON e.entry IN (i.RandomProperty, i.RandomSuffix)
    WHERE (i.RandomProperty > 0 OR i.RandomSuffix > 0) AND e.entry IS NULL AND i.entry IN (SELECT item_entry FROM manastorm_greedy_demon_loot_pool);
SELECT 'creature: spawn template missing', COUNT(*) FROM creature c LEFT JOIN creature_template t ON t.entry = c.id WHERE t.entry IS NULL;
SELECT 'creature: segment spawn missing', COUNT(*) FROM manastorm_segment_spawn s LEFT JOIN creature c ON c.guid = s.spawn_guid WHERE c.guid IS NULL;
SELECT 'creature: chaotic link spawn missing', COUNT(*) FROM manastorm_chaotic_link_spawn s LEFT JOIN creature c ON c.guid = s.spawn_guid WHERE c.guid IS NULL;
SELECT 'creature: private creature template missing', COUNT(*) FROM manastorm_private_creature p LEFT JOIN creature_template t ON t.entry = p.id WHERE t.entry IS NULL;
SELECT 'creature: affix/greedy/keeper creature missing', COUNT(*) FROM (
    SELECT helper_creature_entry_1 e FROM manastorm_affix_catalog WHERE helper_creature_entry_1
    UNION ALL SELECT helper_creature_entry_2 FROM manastorm_affix_catalog WHERE helper_creature_entry_2
    UNION ALL SELECT creature_entry FROM manastorm_greedy_demon_rule UNION ALL SELECT creature_entry FROM manastorm_treasure_keeper_rule) q
    LEFT JOIN creature_template t ON t.entry = q.e WHERE t.entry IS NULL;
SELECT 'info: enabled segments whose objective creature is missing (missing in CoA too)', COUNT(*) FROM manastorm_segment s
    LEFT JOIN creature_template t ON t.entry = s.objective_creature_entry WHERE s.enabled AND s.objective_creature_entry AND t.entry IS NULL;
SELECT 'creature: template without model', COUNT(*) FROM creature_template t LEFT JOIN creature_template_model m ON m.CreatureID = t.entry
    WHERE m.CreatureID IS NULL AND t.entry IN (SELECT id FROM creature);
SELECT 'segment: start teleport missing', COUNT(*) FROM manastorm_segment s LEFT JOIN game_tele t ON t.id = s.start_game_tele_id
    WHERE s.start_game_tele_id AND t.id IS NULL;
SELECT 'hub: return gameobject missing', COUNT(*) FROM manastorm_hub_return h LEFT JOIN gameobject g ON g.guid = h.gameobject_guid WHERE g.guid IS NULL;
SELECT 'gameobject: spawn template missing', COUNT(*) FROM gameobject g LEFT JOIN gameobject_template t ON t.entry = g.id WHERE t.entry IS NULL;
SELECT 'info: item_template rows', COUNT(*) FROM item_template;
SELECT 'info: remapped items present', COUNT(*) FROM item_template WHERE entry BETWEEN 3300001 AND 3399999;
SELECT 'item: greedy pool tier differs from item quality (module refuses to load)', COUNT(*)
    FROM manastorm_greedy_demon_loot_pool p JOIN item_template i ON i.entry = p.item_entry
    WHERE p.enabled = 1 AND i.Quality <> p.quality_tier;
SELECT 'manastorm: endgame scaling curve empty (endgame depths unscaled)', COUNT(*) = 0 FROM manastorm_Endgame_scaling_curve;
SELECT 'spell: manastorm spell script binding missing', COUNT(*) FROM ms_coa_world.spell_script_names s
    LEFT JOIN ms_coa_world.ms_spell_remap r ON r.id = ABS(s.spell_id)
    LEFT JOIN spell_script_names e ON e.ScriptName = s.ScriptName AND e.spell_id = SIGN(s.spell_id) * COALESCE(r.new_id, ABS(s.spell_id))
    WHERE s.ScriptName LIKE 'spell\_manastorm%' AND e.spell_id IS NULL;
