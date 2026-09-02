-- ==========================================================
-- libcore WebBoard — MySQL / MariaDB Schema
-- v1.7.2 | charset: utf8mb4
-- ==========================================================

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

-- ----------------------------------------------------------
-- board_members
-- ----------------------------------------------------------
DROP TABLE IF EXISTS `board_members`;
CREATE TABLE `board_members` (
  `id`         INT(11)      NOT NULL AUTO_INCREMENT,
  `username`   VARCHAR(50)  NOT NULL,
  `password`   VARCHAR(255) NOT NULL,
  `nickname`   VARCHAR(50)  NOT NULL,
  `email`      VARCHAR(100) NOT NULL,
  `role`       TINYINT(4)   NOT NULL DEFAULT 0,
  `is_active`  TINYINT(4)   NOT NULL DEFAULT 1,
  `created_at` DATETIME     NOT NULL DEFAULT current_timestamp(),
  `last_login` DATETIME              DEFAULT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_members_username` (`username`),
  UNIQUE KEY `uk_members_email`    (`email`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_uca1400_ai_ci;

INSERT INTO `board_members` VALUES
(1,'admin', '$2b$12$KIXtfCDge3RR.N5HGXdQzeLCNb5eJMbvpkX4FZ7vVoknFHa8RYMHK','관리자','admin@domain.com', 1,1,NULL,NULL);

-- ----------------------------------------------------------
-- board_posts
-- ----------------------------------------------------------
DROP TABLE IF EXISTS `board_posts`;
CREATE TABLE `board_posts` (
  `id`         INT(11)      NOT NULL AUTO_INCREMENT,
  `member_id`  INT(11)      NOT NULL,
  `title`      VARCHAR(200) NOT NULL,
  `content`    TEXT         NOT NULL,
  `view_count` INT(11)      NOT NULL DEFAULT 0,
  `is_deleted` TINYINT(4)   NOT NULL DEFAULT 0,
  `created_at` DATETIME     NOT NULL DEFAULT current_timestamp(),
  `updated_at` DATETIME     NOT NULL DEFAULT current_timestamp()
                                      ON UPDATE current_timestamp(),
  `deleted_at` DATETIME              DEFAULT NULL,
  `is_secret`  VARCHAR(255)          DEFAULT NULL,
  PRIMARY KEY (`id`),
  KEY `idx_posts_member_id` (`member_id`),
  KEY `idx_posts_list`      (`is_deleted`, `created_at` DESC),
  CONSTRAINT `fk_posts_member`
    FOREIGN KEY (`member_id`) REFERENCES `board_members` (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_uca1400_ai_ci;


-- ----------------------------------------------------------
-- board_attachments
-- ----------------------------------------------------------
DROP TABLE IF EXISTS `board_attachments`;
CREATE TABLE `board_attachments` (
  `id`         INT(11)      NOT NULL AUTO_INCREMENT,
  `post_id`    INT(11)      NOT NULL,
  `member_id`  INT(11)      NOT NULL,
  `file_name`  VARCHAR(255) NOT NULL,
  `saved_name` VARCHAR(512) NOT NULL,
  `file_size`  BIGINT(20)   NOT NULL DEFAULT 0,
  `mime_type`  VARCHAR(100)          DEFAULT NULL,
  `is_deleted` TINYINT(4)   NOT NULL DEFAULT 0,
  `created_at` DATETIME     NOT NULL DEFAULT current_timestamp(),
  PRIMARY KEY (`id`),
  KEY `idx_attach_post_id`    (`post_id`),
  KEY `idx_attach_member_id`  (`member_id`),
  CONSTRAINT `fk_attach_post`
    FOREIGN KEY (`post_id`)   REFERENCES `board_posts`   (`id`),
  CONSTRAINT `fk_attach_member`
    FOREIGN KEY (`member_id`) REFERENCES `board_members` (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_uca1400_ai_ci;


-- ----------------------------------------------------------
-- board_comments
-- ----------------------------------------------------------
DROP TABLE IF EXISTS `board_comments`;
CREATE TABLE `board_comments` (
  `id`         INT(11)    NOT NULL AUTO_INCREMENT,
  `post_id`    INT(11)    NOT NULL,
  `member_id`  INT(11)    NOT NULL,
  `parent_id`  INT(11)             DEFAULT NULL,
  `depth`      TINYINT(4) NOT NULL DEFAULT 0,
  `content`    TEXT       NOT NULL,
  `is_deleted` TINYINT(4) NOT NULL DEFAULT 0,
  `created_at` DATETIME   NOT NULL DEFAULT current_timestamp(),
  PRIMARY KEY (`id`),
  KEY `idx_comments_member_id`  (`member_id`),
  KEY `idx_comments_parent_id`  (`parent_id`),
  KEY `idx_comments_list`       (`post_id`, `is_deleted`, `parent_id`),
  CONSTRAINT `fk_comments_post`
    FOREIGN KEY (`post_id`)   REFERENCES `board_posts`    (`id`),
  CONSTRAINT `fk_comments_member`
    FOREIGN KEY (`member_id`) REFERENCES `board_members`  (`id`),
  CONSTRAINT `fk_comments_parent`
    FOREIGN KEY (`parent_id`) REFERENCES `board_comments` (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_uca1400_ai_ci;

-- ----------------------------------------------------------
-- board_config
-- ----------------------------------------------------------
DROP TABLE IF EXISTS `board_config`;
CREATE TABLE `board_config` (
  `id`        INT(11)      NOT NULL AUTO_INCREMENT,
  `cfg_name`  VARCHAR(128) NOT NULL,
  `cfg_value` TEXT         NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_board_config_name` (`cfg_name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_uca1400_ai_ci;


INSERT INTO board_config (cfg_name, cfg_value) VALUES
('skin',       'white'),
('board_list', '20');
SET FOREIGN_KEY_CHECKS = 1;
