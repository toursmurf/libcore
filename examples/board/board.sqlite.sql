-- ==========================================================
-- libcore WebBoard — SQLite Schema
-- v1.7.2
-- ==========================================================

PRAGMA foreign_keys = OFF;

-- ==========================================================
-- 기존 객체 제거
-- 자식 테이블부터 제거
-- ==========================================================

DROP TRIGGER IF EXISTS `trg_board_posts_updated_at`;

DROP TABLE IF EXISTS `board_comments`;
DROP TABLE IF EXISTS `board_attachments`;
DROP TABLE IF EXISTS `board_posts`;
DROP TABLE IF EXISTS `board_config`;
DROP TABLE IF EXISTS `board_members`;


-- ----------------------------------------------------------
-- board_members
-- ----------------------------------------------------------

CREATE TABLE `board_members` (
  `id`         INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  `username`   TEXT    NOT NULL,
  `password`   TEXT    NOT NULL,
  `nickname`   TEXT    NOT NULL,
  `email`      TEXT    NOT NULL,
  `role`       INTEGER NOT NULL DEFAULT 0,
  `is_active`  INTEGER NOT NULL DEFAULT 1,

  `created_at` TEXT    NOT NULL
                       DEFAULT (datetime('now', 'localtime')),

  `last_login` TEXT             DEFAULT NULL,

  CONSTRAINT `uk_members_username`
    UNIQUE (`username`),

  CONSTRAINT `uk_members_email`
    UNIQUE (`email`)
);

INSERT INTO `board_members`(`id`,`username`,`password`,`nickname`,`email`,`role`,`is_active`,`created_at`,`last_login`)
VALUES    (1,'admin','$2b$12$KIXtfCDge3RR.N5HGXdQzeLCNb5eJMbvpkX4FZ7vVoknFHa8RYMHK','관리자','admin@toos.it',1,1,NULL,NULL);

-- ----------------------------------------------------------
-- board_posts
-- ----------------------------------------------------------

CREATE TABLE `board_posts` (
  `id`         INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  `member_id`  INTEGER NOT NULL,
  `title`      TEXT    NOT NULL,
  `content`    TEXT    NOT NULL,
  `view_count` INTEGER NOT NULL DEFAULT 0,
  `is_deleted` INTEGER NOT NULL DEFAULT 0,

  `created_at` TEXT    NOT NULL
                       DEFAULT (datetime('now', 'localtime')),

  `updated_at` TEXT    NOT NULL
                       DEFAULT (datetime('now', 'localtime')),

  `deleted_at` TEXT             DEFAULT NULL,
  `is_secret`  TEXT             DEFAULT NULL,

  CONSTRAINT `fk_posts_member`
    FOREIGN KEY (`member_id`)
    REFERENCES `board_members` (`id`)
);


CREATE INDEX `idx_posts_member_id`
ON `board_posts` (`member_id`);


CREATE INDEX `idx_posts_list`
ON `board_posts` (`is_deleted`, `created_at` DESC);


-- MySQL:
--
-- updated_at DATETIME NOT NULL
-- DEFAULT current_timestamp()
-- ON UPDATE current_timestamp()
--
-- 대응용 trigger
CREATE TRIGGER `trg_board_posts_updated_at`
AFTER UPDATE OF
  `member_id`,
  `title`,
  `content`,
  `view_count`,
  `is_deleted`,
  `deleted_at`,
  `is_secret`
ON `board_posts`
FOR EACH ROW
BEGIN
  UPDATE `board_posts`
     SET `updated_at` = datetime('now', 'localtime')
   WHERE `id` = NEW.`id`;
END;

-- ----------------------------------------------------------
-- board_attachments
-- ----------------------------------------------------------

CREATE TABLE `board_attachments` (
  `id`         INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  `post_id`    INTEGER NOT NULL,
  `member_id`  INTEGER NOT NULL,
  `file_name`  TEXT    NOT NULL,
  `saved_name` TEXT    NOT NULL,
  `file_size`  INTEGER NOT NULL DEFAULT 0,
  `mime_type`  TEXT             DEFAULT NULL,
  `is_deleted` INTEGER NOT NULL DEFAULT 0,

  `created_at` TEXT    NOT NULL
                       DEFAULT (datetime('now', 'localtime')),

  CONSTRAINT `fk_attach_post`
    FOREIGN KEY (`post_id`)
    REFERENCES `board_posts` (`id`),

  CONSTRAINT `fk_attach_member`
    FOREIGN KEY (`member_id`)
    REFERENCES `board_members` (`id`)
);

CREATE INDEX `idx_attach_post_id`
ON `board_attachments` (`post_id`);

CREATE INDEX `idx_attach_member_id`
ON `board_attachments` (`member_id`);


-- ----------------------------------------------------------
-- board_comments
-- ----------------------------------------------------------
CREATE TABLE `board_comments` (
  `id`         INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  `post_id`    INTEGER NOT NULL,
  `member_id`  INTEGER NOT NULL,
  `parent_id`  INTEGER          DEFAULT NULL,
  `depth`      INTEGER NOT NULL DEFAULT 0,
  `content`    TEXT    NOT NULL,
  `is_deleted` INTEGER NOT NULL DEFAULT 0,

  `created_at` TEXT    NOT NULL
 DEFAULT (datetime('now', 'localtime')),

  CONSTRAINT `fk_comments_post`
    FOREIGN KEY (`post_id`)
    REFERENCES `board_posts` (`id`),

  CONSTRAINT `fk_comments_member`
    FOREIGN KEY (`member_id`)
    REFERENCES `board_members` (`id`),

  CONSTRAINT `fk_comments_parent`
    FOREIGN KEY (`parent_id`)
    REFERENCES `board_comments` (`id`)
);


CREATE INDEX `idx_comments_member_id` ON `board_comments` (`member_id`);
CREATE INDEX `idx_comments_parent_id` ON `board_comments` (`parent_id`);
CREATE INDEX `idx_comments_list`      ON `board_comments` (`post_id`,`is_deleted`,`parent_id`);


-- ----------------------------------------------------------
-- board_config
-- ----------------------------------------------------------

CREATE TABLE `board_config` (
  `id`        INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  `cfg_name`  TEXT    NOT NULL,
  `cfg_value` TEXT    NOT NULL,

  CONSTRAINT `uk_board_config_name`
    UNIQUE (`cfg_name`)
);

INSERT INTO `board_config` (`cfg_name`,`cfg_value`)  VALUES ( 'skin','white'),('board_list','20');

-- ==========================================================
-- Foreign Key 활성화
-- ==========================================================
PRAGMA foreign_keys = ON;
-- ==========================================================
-- 초기 데이터 FK 무결성 확인
-- 결과가 없어야 정상
-- ==========================================================
PRAGMA foreign_key_check;
