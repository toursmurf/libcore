INSERT INTO toos_type_categories
    (id, code, name_ko, name_en, sort_order, enabled)
VALUES
    (1, 'daily', '일상', 'Daily Life', 1, 1);

INSERT INTO toos_type_sentences
    (id, category_id, difficulty_ko, difficulty_en, ko_text, en_text, enabled)
VALUES
    (1, 1, 1, 1, '좋은 아침입니다.', 'Good morning.', 1),
    (2, 1, 1, 1, '오늘 날씨가 좋네요.', 'The weather is nice today.', 1),
    (3, 1, 1, 1, '만나서 반갑습니다.', 'Nice to meet you.', 1),
    (4, 1, 1, 1, '좋은 하루 보내세요.', 'Have a nice day.', 1),
    (5, 1, 1, 1, '잠시만 기다려 주세요.', 'Please wait a moment.', 1);
