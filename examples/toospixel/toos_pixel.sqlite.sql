CREATE TABLE IF NOT EXISTS game_settings (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
INSERT OR IGNORE INTO game_settings (key, value) VALUES ('board_width', '64');
INSERT OR IGNORE INTO game_settings (key, value) VALUES ('board_height', '64');