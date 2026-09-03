/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `ap_account_progress` (
  `account_id` int unsigned NOT NULL,
  `absorb_bonus` float NOT NULL DEFAULT '0',
  PRIMARY KEY (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `ap_aether_sinks` (
  `account_id` int unsigned NOT NULL,
  `category` varchar(32) NOT NULL,
  `invested` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`account_id`,`category`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `ap_item_attune` (
  `guid` int unsigned NOT NULL,
  `item_entry` int unsigned NOT NULL,
  `progress` int unsigned NOT NULL DEFAULT '0',
  `attuned` tinyint(1) NOT NULL DEFAULT '0',
  PRIMARY KEY (`guid`,`item_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `ap_item_snapshot` (
  `guid` int unsigned NOT NULL,
  `item_entry` int unsigned NOT NULL,
  `quality` tinyint unsigned NOT NULL DEFAULT '1',
  `str` float NOT NULL DEFAULT '0',
  `agi` float NOT NULL DEFAULT '0',
  `sta` float NOT NULL DEFAULT '0',
  `int` float NOT NULL DEFAULT '0',
  `spi` float NOT NULL DEFAULT '0',
  `armor` float NOT NULL DEFAULT '0',
  `weapon_dps` float NOT NULL DEFAULT '0',
  PRIMARY KEY (`guid`,`item_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `ap_reagent_archive` (
  `account_id` int unsigned NOT NULL,
  `item_entry` int unsigned NOT NULL,
  `count` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`account_id`,`item_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `ap_talents` (
  `guid` int unsigned NOT NULL,
  `stat_index` tinyint unsigned NOT NULL,
  `rank` tinyint unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`guid`,`stat_index`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_absorb` (
  `account_id` int unsigned NOT NULL,
  `item_entry` int unsigned NOT NULL,
  `str` float NOT NULL DEFAULT '0',
  `agi` float NOT NULL DEFAULT '0',
  `sta` float NOT NULL DEFAULT '0',
  `intel` float NOT NULL DEFAULT '0',
  `spi` float NOT NULL DEFAULT '0',
  `armor` float NOT NULL DEFAULT '0',
  `sec_crit` float NOT NULL DEFAULT '0',
  `sec_hit` float NOT NULL DEFAULT '0',
  `sec_haste` float NOT NULL DEFAULT '0',
  `sec_expertise` float NOT NULL DEFAULT '0',
  `sec_armor_pen` float NOT NULL DEFAULT '0',
  `sec_resilience` float NOT NULL DEFAULT '0',
  `sec_attack_power` float NOT NULL DEFAULT '0',
  `sec_spell_power` float NOT NULL DEFAULT '0',
  `sec_defense` float NOT NULL DEFAULT '0',
  `sec_dodge` float NOT NULL DEFAULT '0',
  `sec_parry` float NOT NULL DEFAULT '0',
  `sec_block` float NOT NULL DEFAULT '0',
  `sec_mp5` float NOT NULL DEFAULT '0',
  `sec_health_regen` float NOT NULL DEFAULT '0',
  `sec_spell_pen` float NOT NULL DEFAULT '0',
  `sec_block_value` float NOT NULL DEFAULT '0',
  `item_level` smallint unsigned NOT NULL DEFAULT '1',
  `attune_pct` smallint unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`account_id`,`item_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_account_currency` (
  `account_id` int unsigned NOT NULL,
  `item_id` int unsigned NOT NULL,
  `count` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`account_id`,`item_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_account_exalted_faction` (
  `account_id` int unsigned NOT NULL,
  `faction_id` int unsigned NOT NULL,
  PRIMARY KEY (`account_id`,`faction_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_account_key` (
  `account_id` int unsigned NOT NULL,
  `item_entry` int unsigned NOT NULL,
  PRIMARY KEY (`account_id`,`item_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_account_meta` (
  `account_id` int unsigned NOT NULL,
  `corpses_looted` int unsigned NOT NULL DEFAULT '0',
  `fish_caught` int unsigned NOT NULL DEFAULT '0',
  `auto_attune_on` tinyint unsigned NOT NULL DEFAULT '0',
  `auto_attune_off` int unsigned NOT NULL DEFAULT '0',
  `auto_attune_ilvl` smallint unsigned NOT NULL DEFAULT '0',
  `attuned_de` tinyint unsigned NOT NULL DEFAULT '0',
  `hearth_uses` int unsigned NOT NULL DEFAULT '0',
  `jump_mode` tinyint unsigned NOT NULL DEFAULT '2',
  `ui_scale` tinyint unsigned NOT NULL DEFAULT '100',
  `shared_gold` int unsigned NOT NULL DEFAULT '0',
  `shared_honor` int unsigned NOT NULL DEFAULT '0',
  `shared_arena` int unsigned NOT NULL DEFAULT '0',
  `taxi_mask` varchar(255) NOT NULL DEFAULT '',
  `shared_inited` tinyint unsigned NOT NULL DEFAULT '0',
  `solo_queue` tinyint unsigned NOT NULL DEFAULT '0',
  `auto_mount` tinyint unsigned NOT NULL DEFAULT '0',
  `speed_cap` int unsigned NOT NULL DEFAULT '500',
  `riding_skill` int unsigned NOT NULL DEFAULT '0',
  `autoloot_on` tinyint unsigned NOT NULL DEFAULT '1',
  `autoloot_de` tinyint unsigned NOT NULL DEFAULT '0',
  `pull_radius` tinyint unsigned NOT NULL DEFAULT '0',
  `track_ore` tinyint unsigned NOT NULL DEFAULT '0',
  `track_herb` tinyint unsigned NOT NULL DEFAULT '0',
  `last_respec` int unsigned NOT NULL DEFAULT '0',
  `perk_epoch` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_account_mount` (
  `account_id` int unsigned NOT NULL,
  `spell_id` int unsigned NOT NULL,
  PRIMARY KEY (`account_id`,`spell_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_account_perk` (
  `account_id` int unsigned NOT NULL,
  `spell_id` int unsigned NOT NULL,
  PRIMARY KEY (`account_id`,`spell_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_account_perk_purchase` (
  `account_id` int unsigned NOT NULL,
  `spell_id` int unsigned NOT NULL,
  `paid` int unsigned NOT NULL,
  PRIMARY KEY (`account_id`,`spell_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_account_recipe` (
  `account_id` int unsigned NOT NULL,
  `spell_id` int unsigned NOT NULL,
  PRIMARY KEY (`account_id`,`spell_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_account_rep` (
  `account_id` int unsigned NOT NULL,
  `faction_id` smallint unsigned NOT NULL,
  `standing` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`account_id`,`faction_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_account_reputation` (
  `account_id` int unsigned NOT NULL,
  `faction_id` int unsigned NOT NULL,
  `standing` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`account_id`,`faction_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_account_skill` (
  `account_id` int unsigned NOT NULL,
  `skill_id` smallint unsigned NOT NULL,
  `skill_value` smallint unsigned NOT NULL DEFAULT '0',
  `skill_max` smallint unsigned NOT NULL DEFAULT '0',
  `skill_step` smallint unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`account_id`,`skill_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_account_taxi_maps` (
  `account_id` int unsigned NOT NULL,
  `map_id` smallint unsigned NOT NULL,
  `unlocked_at` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`account_id`,`map_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_achievement_value` (
  `achievement_id` int unsigned NOT NULL,
  `skill_points` int unsigned NOT NULL,
  PRIMARY KEY (`achievement_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_attune_milestone` (
  `account_id` int unsigned NOT NULL,
  `milestone` int unsigned NOT NULL,
  `earned_at` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`account_id`,`milestone`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_autoloot_rule` (
  `account_id` int unsigned NOT NULL,
  `sort_idx` tinyint unsigned NOT NULL,
  `match_type` tinyint unsigned NOT NULL,
  `action` tinyint unsigned NOT NULL,
  `negate` tinyint unsigned NOT NULL DEFAULT '0',
  `quality` tinyint unsigned NOT NULL DEFAULT '0',
  `match_text` varchar(64) NOT NULL DEFAULT '',
  PRIMARY KEY (`account_id`,`sort_idx`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_bug_report` (
  `id` int unsigned NOT NULL AUTO_INCREMENT,
  `report_type` varchar(16) NOT NULL DEFAULT 'bug',
  `is_critical` tinyint unsigned NOT NULL DEFAULT '0',
  `is_recurring` tinyint unsigned NOT NULL DEFAULT '0',
  `account_id` int unsigned NOT NULL,
  `character_guid` int unsigned NOT NULL,
  `character_name` varchar(24) NOT NULL DEFAULT '',
  `reported_at` int unsigned NOT NULL DEFAULT '0',
  `map_id` int unsigned NOT NULL DEFAULT '0',
  `zone_id` int unsigned NOT NULL DEFAULT '0',
  `zone_name` varchar(100) NOT NULL DEFAULT '',
  `pos_x` float NOT NULL DEFAULT '0',
  `pos_y` float NOT NULL DEFAULT '0',
  `pos_z` float NOT NULL DEFAULT '0',
  `player_level` tinyint unsigned NOT NULL DEFAULT '0',
  `target_entry` int unsigned NOT NULL DEFAULT '0',
  `target_name` varchar(100) NOT NULL DEFAULT '',
  `description` text NOT NULL,
  `posted` tinyint unsigned NOT NULL DEFAULT '0',
  `status` varchar(16) NOT NULL DEFAULT 'open',
  `resolution` text,
  `resolved_at` int unsigned NOT NULL DEFAULT '0',
  `discord_message_id` varchar(32) NOT NULL DEFAULT '',
  `github_issue_number` int unsigned DEFAULT NULL,
  `github_issue_url` varchar(255) NOT NULL DEFAULT '',
  `github_synced_at` int unsigned NOT NULL DEFAULT '0',
  `github_sync_attempts` smallint unsigned NOT NULL DEFAULT '0',
  `github_sync_error` varchar(500) NOT NULL DEFAULT '',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_github_issue_number` (`github_issue_number`),
  KEY `idx_posted` (`posted`,`id`),
  KEY `idx_account` (`account_id`),
  KEY `idx_status` (`status`,`id`),
  KEY `idx_github_sync` (`status`,`report_type`,`github_issue_number`,`id`),
  KEY `idx_priority` (`is_critical`,`posted`,`id`)
) ENGINE=InnoDB AUTO_INCREMENT=256 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_char_class_grant` (
  `guid` int unsigned NOT NULL,
  `spell_id` int unsigned NOT NULL,
  PRIMARY KEY (`guid`,`spell_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_char_class_perk` (
  `guid` int unsigned NOT NULL,
  `spell_id` int unsigned NOT NULL,
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_char_class_perk_spec` (
  `guid` int unsigned NOT NULL,
  `spec` tinyint unsigned NOT NULL,
  `spell_id` int unsigned NOT NULL,
  PRIMARY KEY (`guid`,`spec`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_char_rep_down` (
  `guid` int unsigned NOT NULL,
  `faction_id` smallint unsigned NOT NULL,
  PRIMARY KEY (`guid`,`faction_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_char_spec` (
  `guid` int unsigned NOT NULL,
  `spec_spell` int unsigned NOT NULL,
  PRIMARY KEY (`guid`,`spec_spell`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_class_buff_unlock` (
  `account_id` int unsigned NOT NULL,
  `class` tinyint unsigned NOT NULL,
  PRIMARY KEY (`account_id`,`class`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_combo` (
  `guid` int unsigned NOT NULL,
  `stacks` tinyint unsigned NOT NULL DEFAULT '0',
  `expires` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_item` (
  `item_guid` int unsigned NOT NULL,
  `item_entry` int unsigned NOT NULL,
  `owner_guid` int unsigned NOT NULL,
  `xp` int unsigned NOT NULL DEFAULT '0',
  `level` smallint unsigned NOT NULL DEFAULT '1',
  `roll_str` int NOT NULL DEFAULT '0',
  `roll_agi` int NOT NULL DEFAULT '0',
  `roll_sta` int NOT NULL DEFAULT '0',
  `roll_int` int NOT NULL DEFAULT '0',
  `roll_spi` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`item_guid`),
  KEY `idx_lg_item_owner` (`owner_guid`),
  KEY `idx_lg_item_entry` (`item_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_perk_cost` (
  `spell_id` int unsigned NOT NULL,
  `cost` int unsigned NOT NULL,
  `requires_spell_id` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`spell_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_quest_complete` (
  `account_id` int unsigned NOT NULL,
  `last_used` int unsigned NOT NULL DEFAULT '0',
  `bypass_count` int unsigned NOT NULL DEFAULT '0',
  `window_start` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_vault` (
  `account_id` int unsigned NOT NULL,
  `owner_guid` int unsigned NOT NULL,
  `kind` tinyint unsigned NOT NULL,
  `item_entry` int unsigned NOT NULL,
  `item_count` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`account_id`,`owner_guid`,`kind`,`item_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_vault_reagent` (
  `item_entry` int unsigned NOT NULL,
  PRIMARY KEY (`item_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_wayfarer` (
  `guid` int unsigned NOT NULL,
  `account_id` int unsigned NOT NULL DEFAULT '0',
  `dmg_pct` tinyint unsigned NOT NULL DEFAULT '0',
  `changed_at` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`guid`),
  KEY `idx_account` (`account_id`,`changed_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `z_archive_ap_account_progress` (
  `account_id` int unsigned NOT NULL,
  `absorb_bonus` float NOT NULL DEFAULT '0',
  PRIMARY KEY (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `z_archive_ap_account_taxi` (
  `account_id` int unsigned NOT NULL,
  `node_id` smallint unsigned NOT NULL,
  PRIMARY KEY (`account_id`,`node_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `z_archive_ap_aether_milestones` (
  `account_id` int unsigned NOT NULL,
  `milestone_type` varchar(32) NOT NULL,
  `milestone_id` int unsigned NOT NULL,
  PRIMARY KEY (`account_id`,`milestone_type`,`milestone_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `z_archive_ap_aether_sinks` (
  `account_id` int unsigned NOT NULL,
  `category` varchar(32) NOT NULL,
  `invested` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`account_id`,`category`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `z_archive_ap_attunements` (
  `id` int unsigned NOT NULL AUTO_INCREMENT,
  `preset` varchar(32) NOT NULL,
  `perKillBase` int NOT NULL,
  `bonusBoss` int NOT NULL,
  `capPerItem` int NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uniq_preset` (`preset`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `z_archive_ap_aura_test_results` (
  `guid` int unsigned NOT NULL,
  `spell_id` int unsigned NOT NULL,
  `theme` varchar(16) NOT NULL DEFAULT '',
  `tier` tinyint NOT NULL DEFAULT '0',
  `result` varchar(16) NOT NULL DEFAULT 'UNTESTED',
  `notes` varchar(128) NOT NULL DEFAULT '',
  `tested_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`guid`,`spell_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `z_archive_ap_bg_objectives` (
  `id` int unsigned NOT NULL AUTO_INCREMENT,
  `bg_type_id` tinyint unsigned NOT NULL,
  `objective_id` int unsigned NOT NULL,
  `essence_reward` int unsigned NOT NULL DEFAULT '25',
  `description` varchar(64) DEFAULT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `bg_obj_unique` (`bg_type_id`,`objective_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `z_archive_ap_dissolved_items` (
  `account_id` int unsigned NOT NULL,
  `item_entry` int unsigned NOT NULL,
  PRIMARY KEY (`account_id`,`item_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `z_archive_ap_dungeon_leaderboard` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `map_id` int unsigned NOT NULL,
  `difficulty` tinyint unsigned NOT NULL DEFAULT '0',
  `account_id` int unsigned NOT NULL,
  `char_name` varchar(32) NOT NULL DEFAULT '',
  `clear_ms` int unsigned NOT NULL,
  `ts` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  KEY `idx_map_time` (`map_id`,`difficulty`,`clear_ms`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `z_archive_ap_dungeon_pb` (
  `account_id` int unsigned NOT NULL,
  `map_id` int unsigned NOT NULL,
  `difficulty` tinyint unsigned NOT NULL DEFAULT '0',
  `best_ms` int unsigned NOT NULL,
  `char_name` varchar(32) NOT NULL DEFAULT '',
  `updated_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`,`map_id`,`difficulty`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `z_archive_ap_item_attune` (
  `guid` int unsigned NOT NULL,
  `item_entry` int unsigned NOT NULL,
  `progress` int unsigned NOT NULL DEFAULT '0',
  `attuned` tinyint(1) NOT NULL DEFAULT '0',
  PRIMARY KEY (`guid`,`item_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `z_archive_ap_item_snapshot` (
  `guid` int unsigned NOT NULL,
  `item_entry` int unsigned NOT NULL,
  `quality` tinyint unsigned NOT NULL DEFAULT '1',
  `str` float NOT NULL DEFAULT '0',
  `agi` float NOT NULL DEFAULT '0',
  `sta` float NOT NULL DEFAULT '0',
  `int` float NOT NULL DEFAULT '0',
  `spi` float NOT NULL DEFAULT '0',
  `armor` float NOT NULL DEFAULT '0',
  `weapon_dps` float NOT NULL DEFAULT '0',
  PRIMARY KEY (`guid`,`item_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `z_archive_ap_mastery` (
  `guid` int unsigned NOT NULL,
  `aether` bigint unsigned NOT NULL DEFAULT '0',
  `mastery` int unsigned NOT NULL DEFAULT '0',
  `rate_xp` float NOT NULL DEFAULT '1',
  `rate_aether` float NOT NULL DEFAULT '1',
  `rate_boss` float NOT NULL DEFAULT '1',
  `rack_slots` tinyint NOT NULL DEFAULT '3',
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `z_archive_ap_mastery_spend` (
  `id` int unsigned NOT NULL AUTO_INCREMENT,
  `guid` int unsigned NOT NULL,
  `amount` int unsigned NOT NULL DEFAULT '0',
  `ts` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  KEY `idx_guid` (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `z_archive_ap_milestone_defs` (
  `id` int unsigned NOT NULL,
  `label` varchar(64) NOT NULL DEFAULT '',
  `aether_reward` int unsigned NOT NULL DEFAULT '50',
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `z_archive_ap_milestones` (
  `guid` int unsigned NOT NULL,
  `milestone_id` int unsigned NOT NULL,
  `ts` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`guid`,`milestone_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `z_archive_ap_quest_rewarded` (
  `guid` int unsigned NOT NULL,
  `quest_id` int unsigned NOT NULL,
  PRIMARY KEY (`guid`,`quest_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `z_archive_ap_rack` (
  `guid` int unsigned NOT NULL,
  `slot_index` tinyint NOT NULL,
  `item_entry` int unsigned NOT NULL DEFAULT '0',
  `item_name` varchar(64) NOT NULL DEFAULT '',
  `item_quality` tinyint NOT NULL DEFAULT '1',
  PRIMARY KEY (`guid`,`slot_index`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `z_archive_ap_reagent_archive` (
  `account_id` int unsigned NOT NULL,
  `item_entry` int unsigned NOT NULL,
  `count` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`account_id`,`item_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `z_archive_ap_residue` (
  `account_id` int unsigned NOT NULL,
  `amount` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `z_archive_ap_resonant_drops` (
  `account_id` int unsigned NOT NULL,
  `item_entry` int unsigned NOT NULL,
  `drop_count` int unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`account_id`,`item_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `z_archive_ap_session_state` (
  `guid` int unsigned NOT NULL,
  `clean_exit` tinyint(1) NOT NULL DEFAULT '0',
  `last_update` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  `threat_level` tinyint unsigned NOT NULL DEFAULT '0',
  `threat_momentum` float NOT NULL DEFAULT '0',
  `threat_debt_kills` smallint unsigned NOT NULL DEFAULT '0',
  `threat_debt_mult` float NOT NULL DEFAULT '1',
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `z_archive_ap_sink_allocation` (
  `guid` int unsigned NOT NULL,
  `category` varchar(32) NOT NULL,
  `allocation` float NOT NULL DEFAULT '0',
  PRIMARY KEY (`guid`,`category`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `z_archive_ap_slot_mastery` (
  `guid` int unsigned NOT NULL,
  `slot` tinyint unsigned NOT NULL,
  `xp` bigint unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`guid`,`slot`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `z_archive_ap_talents` (
  `guid` int unsigned NOT NULL,
  `stat_index` tinyint unsigned NOT NULL,
  `rank` tinyint unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`guid`,`stat_index`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `z_archive_ap_telemetry` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `guid` int unsigned NOT NULL,
  `event` varchar(32) NOT NULL DEFAULT '',
  `value` float NOT NULL DEFAULT '0',
  `ts` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  KEY `idx_guid_event` (`guid`,`event`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `z_archive_ap_visage` (
  `guid` int unsigned NOT NULL,
  `primary_theme` varchar(32) NOT NULL DEFAULT 'worldsoul',
  `primary_enabled` tinyint NOT NULL DEFAULT '1',
  `secondary_theme` varchar(32) NOT NULL DEFAULT 'worldsoul',
  `secondary_enabled` tinyint NOT NULL DEFAULT '1',
  `flash_enabled` tinyint NOT NULL DEFAULT '1',
  `chat_flavor_enabled` tinyint NOT NULL DEFAULT '1',
  `primary_tier_selected` tinyint unsigned NOT NULL DEFAULT '0',
  `secondary_tier_selected` tinyint unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
