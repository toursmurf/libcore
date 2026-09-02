-- ==========================================================
-- libcore WebBoard — PostgreSQL Schema
-- v1.7.2 | encoding: UTF8
-- ==========================================================

-- ----------------------------------------------------------
-- board_members
-- ----------------------------------------------------------
DROP TABLE IF EXISTS board_members CASCADE;
CREATE TABLE board_members (
  id          SERIAL       NOT NULL,
  username    VARCHAR(50)  NOT NULL,
  password    VARCHAR(255) NOT NULL,
  nickname    VARCHAR(50)  NOT NULL,
  email       VARCHAR(100) NOT NULL,
  role        SMALLINT     NOT NULL DEFAULT 0,
  is_active   SMALLINT     NOT NULL DEFAULT 1,
  created_at  TIMESTAMP    NOT NULL DEFAULT NOW(),
  last_login  TIMESTAMP             DEFAULT NULL,
  PRIMARY KEY (id),
  CONSTRAINT uk_members_username UNIQUE (username),
  CONSTRAINT uk_members_email    UNIQUE (email)
);

INSERT INTO board_members (id, username, password, nickname, email, role, is_active, last_login)   VALUES
(1,'admin', '','관리자','admin@toos.it', 1,1,NULL);
-- ----------------------------------------------------------
-- board_posts
-- ----------------------------------------------------------
DROP TABLE IF EXISTS board_posts CASCADE;
CREATE TABLE board_posts (
  id          SERIAL       NOT NULL,
  member_id   INT          NOT NULL,
  title       VARCHAR(200) NOT NULL,
  content     TEXT         NOT NULL,
  view_count  INT          NOT NULL DEFAULT 0,
  is_deleted  SMALLINT     NOT NULL DEFAULT 0,
  created_at  TIMESTAMP    NOT NULL DEFAULT NOW(),
  updated_at  TIMESTAMP    NOT NULL DEFAULT NOW(),
  deleted_at  TIMESTAMP             DEFAULT NULL,
  is_secret   VARCHAR(255)          DEFAULT NULL,
  PRIMARY KEY (id),
  CONSTRAINT fk_posts_member
    FOREIGN KEY (member_id) REFERENCES board_members (id)
);

CREATE INDEX idx_posts_member_id ON board_posts (member_id);
CREATE INDEX idx_posts_list      ON board_posts (is_deleted, created_at DESC);

/* updated_at 자동 갱신 트리거 */
CREATE OR REPLACE FUNCTION fn_set_updated_at()
RETURNS TRIGGER AS $$
BEGIN
  NEW.updated_at = NOW();
  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trg_posts_updated_at
  BEFORE UPDATE ON board_posts
  FOR EACH ROW EXECUTE FUNCTION fn_set_updated_at();

-- ----------------------------------------------------------
-- board_attachments
-- ----------------------------------------------------------
DROP TABLE IF EXISTS board_attachments CASCADE;
CREATE TABLE board_attachments (
  id          SERIAL       NOT NULL,
  post_id     INT          NOT NULL,
  member_id   INT          NOT NULL,
  file_name   VARCHAR(255) NOT NULL,
  saved_name  VARCHAR(512) NOT NULL,
  file_size   BIGINT       NOT NULL DEFAULT 0,
  mime_type   VARCHAR(100)          DEFAULT NULL,
  is_deleted  SMALLINT     NOT NULL DEFAULT 0,
  created_at  TIMESTAMP    NOT NULL DEFAULT NOW(),
  PRIMARY KEY (id),
  CONSTRAINT fk_attach_post
    FOREIGN KEY (post_id)   REFERENCES board_posts   (id),
  CONSTRAINT fk_attach_member
    FOREIGN KEY (member_id) REFERENCES board_members (id)
);

CREATE INDEX idx_attach_post_id   ON board_attachments (post_id);
CREATE INDEX idx_attach_member_id ON board_attachments (member_id);


-- ----------------------------------------------------------
-- board_comments
-- ----------------------------------------------------------
DROP TABLE IF EXISTS board_comments CASCADE;
CREATE TABLE board_comments (
  id          SERIAL   NOT NULL,
  post_id     INT      NOT NULL,
  member_id   INT      NOT NULL,
  parent_id   INT               DEFAULT NULL,
  depth       SMALLINT NOT NULL DEFAULT 0,
  content     TEXT     NOT NULL,
  is_deleted  SMALLINT NOT NULL DEFAULT 0,
  created_at  TIMESTAMP NOT NULL DEFAULT NOW(),
  PRIMARY KEY (id),
  CONSTRAINT fk_comments_post
    FOREIGN KEY (post_id)   REFERENCES board_posts    (id),
  CONSTRAINT fk_comments_member
    FOREIGN KEY (member_id) REFERENCES board_members  (id),
  CONSTRAINT fk_comments_parent
    FOREIGN KEY (parent_id) REFERENCES board_comments (id)
);

CREATE INDEX idx_comments_member_id ON board_comments (member_id);
CREATE INDEX idx_comments_parent_id ON board_comments (parent_id);
CREATE INDEX idx_comments_list      ON board_comments (post_id, is_deleted, parent_id);


-- ----------------------------------------------------------
-- board_config
-- ----------------------------------------------------------
DROP TABLE IF EXISTS board_config CASCADE;
CREATE TABLE board_config (
  id        SERIAL       NOT NULL,
  cfg_name  VARCHAR(128) NOT NULL,
  cfg_value TEXT         NOT NULL,
  PRIMARY KEY (id),
  CONSTRAINT uk_board_config_name UNIQUE (cfg_name)
);

INSERT INTO board_config (cfg_name, cfg_value) VALUES
('skin',       'white'),
('board_list', '20');
