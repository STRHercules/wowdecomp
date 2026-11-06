-- Esteria Housing - DRAFT schema (Phase 0). NOT APPLIED anywhere.
-- Convention (as in EsteriaWoW/modules/mod-seasonpass/data/sql/...): module files under
--   modules/mod-esteria-housing/data/sql/db-world/       -> acore_world (static catalog)
--   modules/mod-esteria-housing/data/sql/db-characters/  -> acore_characters (player state)
-- with CREATE TABLE IF NOT EXISTS. AzerothCore's module updater applies them.
-- Engine InnoDB, utf8mb4. Rollback is at the bottom of each section.

-- =========================== db-world ===========================

-- One row per placeable decor. GameObject templates and display rows are generated from this.
CREATE TABLE IF NOT EXISTS `housing_decor_catalog` (
  `decor_id`          INT UNSIGNED      NOT NULL COMMENT 'HouseDecor.ID (retail)',
  `model_fdid`        INT UNSIGNED      NOT NULL COMMENT 'HouseDecor.ModelFileDataID, client loads retail\\<fdid>.m2',
  `display_id`        INT UNSIGNED      NOT NULL COMMENT 'GameObjectDisplayInfo ID, range 60000-69999',
  `gameobject_entry`  INT UNSIGNED      NOT NULL COMMENT 'gameobject_template.entry, range 9500000-9599999',
  `name`              VARCHAR(100)      NOT NULL DEFAULT '',
  `category`          TINYINT UNSIGNED  NOT NULL DEFAULT 0 COMMENT '0 misc 1 furniture 2 accents 3 lighting 4 nature 5 structural',
  `surface`           TINYINT UNSIGNED  NOT NULL DEFAULT 0 COMMENT '0 floor 1 wall 2 ceiling 3 on-decor',
  `budget_cost`       SMALLINT UNSIGNED NOT NULL DEFAULT 1,
  `min_scale`         FLOAT             NOT NULL DEFAULT 1,
  `max_scale`         FLOAT             NOT NULL DEFAULT 1,
  `origin`            ENUM('retail','custom','stock') NOT NULL DEFAULT 'retail' COMMENT 'retail = dev-only until distribution is decided',
  `enabled`           TINYINT UNSIGNED  NOT NULL DEFAULT 0,
  PRIMARY KEY (`decor_id`),
  UNIQUE KEY `uk_gameobject_entry` (`gameobject_entry`),
  KEY `idx_display` (`display_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Interior templates a house can use (MVP: one).
CREATE TABLE IF NOT EXISTS `housing_room_template` (
  `room_template_id`  INT UNSIGNED NOT NULL,
  `map_id`            SMALLINT UNSIGNED NOT NULL,
  `entry_x` FLOAT NOT NULL, `entry_y` FLOAT NOT NULL, `entry_z` FLOAT NOT NULL, `entry_o` FLOAT NOT NULL,
  `min_x` FLOAT NOT NULL, `min_y` FLOAT NOT NULL, `min_z` FLOAT NOT NULL,
  `max_x` FLOAT NOT NULL, `max_y` FLOAT NOT NULL, `max_z` FLOAT NOT NULL,
  `max_decor`         SMALLINT UNSIGNED NOT NULL DEFAULT 100,
  `name`              VARCHAR(100) NOT NULL DEFAULT '',
  PRIMARY KEY (`room_template_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- rollback (world):
-- DROP TABLE IF EXISTS `housing_room_template`;
-- DROP TABLE IF EXISTS `housing_decor_catalog`;
-- Generated gameobject_template rows: DELETE FROM `gameobject_template` WHERE `entry` BETWEEN 9500000 AND 9599999;
-- Generated display rows: DELETE FROM `gameobjectdisplayinfo_dbc` WHERE `ID` BETWEEN 60000 AND 69999;

-- ========================= db-characters =========================

-- One house per account (MVP).
CREATE TABLE IF NOT EXISTS `housing_house` (
  `house_id`          INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `account_id`        INT UNSIGNED NOT NULL,
  `room_template_id`  INT UNSIGNED NOT NULL,
  `revision`          INT UNSIGNED NOT NULL DEFAULT 0,
  `max_decor`         SMALLINT UNSIGNED NOT NULL DEFAULT 100,
  `created_at`        INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'unix time',
  PRIMARY KEY (`house_id`),
  UNIQUE KEY `uk_account` (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Rooms instanced from templates (one per house in MVP; the room graph comes later).
CREATE TABLE IF NOT EXISTS `housing_room` (
  `room_id`           INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `house_id`          INT UNSIGNED NOT NULL,
  `room_template_id`  INT UNSIGNED NOT NULL,
  `slot`              TINYINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`room_id`),
  UNIQUE KEY `uk_house_slot` (`house_id`, `slot`),
  CONSTRAINT `fk_room_house` FOREIGN KEY (`house_id`) REFERENCES `housing_house` (`house_id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Placed decor. The transform is stored exactly as the server finalized it.
CREATE TABLE IF NOT EXISTS `housing_placed_decor` (
  `placed_id`         BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `house_id`          INT UNSIGNED NOT NULL,
  `room_id`           INT UNSIGNED NOT NULL,
  `decor_id`          INT UNSIGNED NOT NULL,
  `pos_x` FLOAT NOT NULL, `pos_y` FLOAT NOT NULL, `pos_z` FLOAT NOT NULL,
  `rot_x` FLOAT NOT NULL DEFAULT 0, `rot_y` FLOAT NOT NULL DEFAULT 0,
  `rot_z` FLOAT NOT NULL DEFAULT 0, `rot_w` FLOAT NOT NULL DEFAULT 1,
  `scale`             FLOAT NOT NULL DEFAULT 1,
  `parent_placed_id`  BIGINT UNSIGNED NULL DEFAULT NULL COMMENT 'later: ride-along on larger decor',
  `placed_by`         INT UNSIGNED NOT NULL COMMENT 'character guid',
  `placed_at`         INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`placed_id`),
  KEY `idx_house` (`house_id`),
  CONSTRAINT `fk_placed_house` FOREIGN KEY (`house_id`) REFERENCES `housing_house` (`house_id`) ON DELETE CASCADE,
  CONSTRAINT `fk_placed_room` FOREIGN KEY (`room_id`) REFERENCES `housing_room` (`room_id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Decor owned by an account (shared across characters, warband-style).
CREATE TABLE IF NOT EXISTS `housing_account_decor` (
  `account_id`        INT UNSIGNED NOT NULL,
  `decor_id`          INT UNSIGNED NOT NULL,
  `quantity_owned`    SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `quantity_placed`   SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`account_id`, `decor_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Who may visit or edit. MVP: only the owner edits; this is for the permission phase.
CREATE TABLE IF NOT EXISTS `housing_permission` (
  `house_id`          INT UNSIGNED NOT NULL,
  `scope`             TINYINT UNSIGNED NOT NULL COMMENT '0 everyone 1 friends 2 guild 3 account',
  `grantee`           INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'account id for scope 3, else 0',
  `can_visit`         TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `can_edit`          TINYINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`house_id`, `scope`, `grantee`),
  CONSTRAINT `fk_perm_house` FOREIGN KEY (`house_id`) REFERENCES `housing_house` (`house_id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Edit log: one row per accepted edit. Gives undo and audit later.
CREATE TABLE IF NOT EXISTS `housing_revision` (
  `house_id`          INT UNSIGNED NOT NULL,
  `revision`          INT UNSIGNED NOT NULL,
  `op`                TINYINT UNSIGNED NOT NULL COMMENT '1 place 2 move 3 remove',
  `placed_id`         BIGINT UNSIGNED NOT NULL,
  `before_json`       TEXT NULL,
  `after_json`        TEXT NULL,
  `character_guid`    INT UNSIGNED NOT NULL,
  `at`                INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`house_id`, `revision`),
  CONSTRAINT `fk_rev_house` FOREIGN KEY (`house_id`) REFERENCES `housing_house` (`house_id`) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- rollback (characters):
-- DROP TABLE IF EXISTS `housing_revision`;
-- DROP TABLE IF EXISTS `housing_permission`;
-- DROP TABLE IF EXISTS `housing_account_decor`;
-- DROP TABLE IF EXISTS `housing_placed_decor`;
-- DROP TABLE IF EXISTS `housing_room`;
-- DROP TABLE IF EXISTS `housing_house`;
