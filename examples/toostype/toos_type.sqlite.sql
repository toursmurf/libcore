CREATE TABLE IF NOT EXISTS toos_type_categories (
    id INTEGER PRIMARY KEY,
    code TEXT NOT NULL UNIQUE,
    name_ko TEXT NOT NULL,
    name_en TEXT NOT NULL,
    sort_order INTEGER NOT NULL DEFAULT 0 CHECK(sort_order >= 0),
    enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0, 1))
);

CREATE TABLE IF NOT EXISTS toos_type_sentences (
    id INTEGER PRIMARY KEY,
    category_id INTEGER NOT NULL,
    difficulty_ko INTEGER NOT NULL CHECK(difficulty_ko BETWEEN 1 AND 3),
    difficulty_en INTEGER NOT NULL CHECK(difficulty_en BETWEEN 1 AND 3),
    ko_text TEXT NOT NULL,
    en_text TEXT NOT NULL,
    enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0, 1)),
    FOREIGN KEY(category_id) REFERENCES toos_type_categories(id)
);

