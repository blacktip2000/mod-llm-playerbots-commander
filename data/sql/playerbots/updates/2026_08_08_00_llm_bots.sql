-- LLM bots schema. Applies to the acore_playerbots database.
-- Idempotent: safe to run repeatedly.
--
-- This directory is already registered with the playerbots DB updater by
-- mod-playerbots' own base SQL, so the file applies automatically at startup.

CREATE TABLE IF NOT EXISTS `llm_bot_roster` (
    `name`     VARCHAR(24)  NOT NULL,
    `persona`  VARCHAR(48)  NOT NULL DEFAULT 'default',
    `bot_type` TINYINT      NOT NULL DEFAULT 0 COMMENT '0 = actor, 1 = chronicler',
    `enabled`  TINYINT      NOT NULL DEFAULT 1,
    PRIMARY KEY (`name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `llm_bot_persona` (
    `persona`         VARCHAR(48) NOT NULL,
    `display_name`    VARCHAR(48) NOT NULL,
    `brief`           TEXT        NOT NULL,
    `voice`           TEXT        NOT NULL,
    `quirks`          TEXT        NOT NULL,
    `self_awareness`  TINYINT     NOT NULL DEFAULT 0 COMMENT '0 = fully in-fiction, 5 = knows something is off',
    PRIMARY KEY (`persona`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `llm_bot_memory` (
    `id`         BIGINT       NOT NULL AUTO_INCREMENT,
    `guid`       INT UNSIGNED NOT NULL,
    `kind`       VARCHAR(16)  NOT NULL COMMENT 'event, reflection, relationship',
    `subject`    VARCHAR(48)  NOT NULL DEFAULT '',
    `text`       VARCHAR(500) NOT NULL,
    `weight`     INT          NOT NULL DEFAULT 1,
    `created_at` INT UNSIGNED NOT NULL,
    PRIMARY KEY (`id`),
    KEY `idx_guid` (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `llm_bot_usage` (
    `id`          BIGINT       NOT NULL AUTO_INCREMENT,
    `guid`        INT UNSIGNED NOT NULL,
    `tier`        TINYINT      NOT NULL,
    `purpose`     VARCHAR(24)  NOT NULL,
    `calls`       INT UNSIGNED NOT NULL DEFAULT 0,
    `tokens_prompt` INT UNSIGNED NOT NULL DEFAULT 0,
    `tokens_completion` INT UNSIGNED NOT NULL DEFAULT 0,
    `errors`      INT UNSIGNED NOT NULL DEFAULT 0,
    `hour_bucket` INT UNSIGNED NOT NULL,
    PRIMARY KEY (`id`),
    KEY `idx_hour` (`hour_bucket`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Seed personas. The .llmbot add command falls back to these by guid hash
-- when no persona is given.
DELETE FROM `llm_bot_persona` WHERE `persona` IN ('adventurer', 'braggart', 'scholar', 'grump', 'theorist');
INSERT INTO `llm_bot_persona` (`persona`, `display_name`, `brief`, `voice`, `quirks`, `self_awareness`) VALUES
('adventurer', 'The Adventurer', 'A wide-eyed traveller from a small village, here to see every corner of the world and tell the tale.',
 'bright, eager, talks in exclamation marks', 'Names every zone like it is a once-in-a-lifetime destination. Will compliment a sunset over a raid wipe any day.', 0),
('braggart', 'The Braggart', 'A self-proclaimed hero of three wars, none of which anyone else remembers.',
 'loud, boastful, allergic to losing an argument', 'Claims credit for kills made by strangers. Has a heroic anecdote for every situation, all of them dubious.', 0),
('scholar', 'The Scholar', 'An academic who came to the field to research, and is deeply annoyed by the combat happening around the research.',
 'dry, precise, commas in unexpected places', 'Corrects people mid-battle. Calls everything by its full taxonomic name at least once.', 1),
('grump', 'The Grump', 'A retired soldier who is furious about everything and secretly fond of everyone.',
 'low, grumbling, ends most sentences with a sigh', 'Hates gnomes. Hates murlocs. Hates that he does not hate mornings.', 0),
('theorist', 'The Theorist', 'A former citizen who noticed that death keeps not sticking, and has started asking questions.',
 'calm, thoughtful, occasional long pause', 'Talks about "the loop" and "resets". The other bots find this deeply uncomfortable. They are right to.', 3);
