/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `charsections_dbc` (
  `Id` int NOT NULL DEFAULT '0',
  `Race` int NOT NULL DEFAULT '0',
  `Gender` int NOT NULL DEFAULT '0',
  `GenType` int NOT NULL DEFAULT '0',
  `TexturePath1` varchar(100) DEFAULT NULL,
  `TexturePath2` varchar(100) DEFAULT NULL,
  `TexturePath3` varchar(100) DEFAULT NULL,
  `Flags` int NOT NULL DEFAULT '0',
  `Type` int NOT NULL DEFAULT '0',
  `Color` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`Id`) USING BTREE
) ENGINE=MyISAM DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci ROW_FORMAT=DYNAMIC;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `creature_multispawn` (
  `spawnId` int unsigned NOT NULL COMMENT 'creature.guid',
  `entry` int unsigned NOT NULL COMMENT 'creature_template.entry',
  PRIMARY KEY (`spawnId`,`entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Additional creature entries for multi-ID spawning';
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `creature_text_options` (
  `CreatureID` int unsigned NOT NULL,
  `GroupID` tinyint unsigned NOT NULL,
  `OptionSetID` tinyint unsigned NOT NULL,
  PRIMARY KEY (`CreatureID`,`GroupID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `creature_text_option_sets` (
  `SetID` tinyint unsigned NOT NULL,
  `Cooldown` int unsigned NOT NULL DEFAULT '0' COMMENT 'Group cooldown in ms before it can fire again',
  `TriggerChance` tinyint unsigned NOT NULL DEFAULT '100' COMMENT '0-100 pct chance to fire at all',
  `PlayerOnly` tinyint unsigned NOT NULL DEFAULT '0' COMMENT 'Only fire if target is a player',
  `comment` varchar(255) COLLATE utf8mb4_unicode_ci DEFAULT '',
  PRIMARY KEY (`SetID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `emotetextsound_dbc` (
  `Id` int NOT NULL DEFAULT '0',
  `EmotesTextId` int NOT NULL DEFAULT '0',
  `RaceId` int NOT NULL DEFAULT '0',
  `SexId` int NOT NULL DEFAULT '0',
  `SoundId` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`Id`) USING BTREE
) ENGINE=MyISAM DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci ROW_FORMAT=DYNAMIC;
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_dungeon_par` (
  `map_id` int unsigned NOT NULL COMMENT 'Map.dbc id',
  `difficulty` tinyint unsigned NOT NULL DEFAULT '0' COMMENT '0=normal, 1=heroic (instance difficulty)',
  `par_sec` int unsigned NOT NULL COMMENT 'Par clear time in seconds',
  PRIMARY KEY (`map_id`,`difficulty`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Living Gear dungeon par times';
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `lg_zone_scale` (
  `zone_id` int unsigned NOT NULL COMMENT 'AreaTable zone id (parent zone, not sub-area)',
  `max_level` tinyint unsigned NOT NULL COMMENT 'Recommended max level for this zone',
  PRIMARY KEY (`zone_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Living Gear zone scale overrides';
/*!40101 SET character_set_client = @saved_cs_client */;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!50503 SET character_set_client = utf8mb4 */;
CREATE TABLE IF NOT EXISTS `playerbots_rpg_races` (
  `id` int NOT NULL AUTO_INCREMENT,
  `entry` int DEFAULT NULL,
  `race` int DEFAULT NULL,
  `minl` int DEFAULT NULL,
  `maxl` int DEFAULT NULL,
  PRIMARY KEY (`id`),
  KEY `entry` (`entry`)
) ENGINE=InnoDB AUTO_INCREMENT=442 DEFAULT CHARSET=utf8mb3;
/*!40101 SET character_set_client = @saved_cs_client */;
