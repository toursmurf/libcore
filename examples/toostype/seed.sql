BEGIN TRANSACTION;

INSERT OR IGNORE INTO toos_type_categories
    (id, code, name_ko, name_en, sort_order, enabled)
VALUES
    (1, 'daily', '일상', 'Daily Life', 1, 1);

INSERT INTO toos_type_sentences
    (id, category_id, difficulty_ko, difficulty_en, ko_text, en_text, enabled)
VALUES
    (1, 1, 1, 1, '좋은 아침입니다.', 'Good morning.', 1),
    (2, 1, 1, 1, '잘 주무셨어요?', 'Did you sleep well?', 1),
    (3, 1, 1, 1, '오늘 기분이 어때요?', 'How are you feeling today?', 1),
    (4, 1, 1, 1, '만나서 반갑습니다.', 'Nice to meet you.', 1),
    (5, 1, 1, 1, '오랜만이에요.', 'It has been a long time.', 1),
    (6, 1, 1, 1, '좋은 하루 보내세요.', 'Have a nice day.', 1),
    (7, 1, 1, 1, '조심히 들어가세요.', 'Get home safely.', 1),
    (8, 1, 1, 1, '내일 다시 만나요.', 'See you again tomorrow.', 1),
    (9, 1, 1, 1, '감사합니다.', 'Thank you.', 1),
    (10, 1, 1, 1, '천만에요.', 'You are welcome.', 1),

    (11, 1, 1, 1, '잠시만 기다려 주세요.', 'Please wait a moment.', 1),
    (12, 1, 1, 1, '괜찮아요.', 'It is okay.', 1),
    (13, 1, 1, 1, '문제없어요.', 'No problem.', 1),
    (14, 1, 1, 1, '알겠습니다.', 'I understand.', 1),
    (15, 1, 1, 1, '잘 모르겠어요.', 'I am not sure.', 1),
    (16, 1, 1, 1, '다시 말씀해 주세요.', 'Please say that again.', 1),
    (17, 1, 1, 1, '천천히 말씀해 주세요.', 'Please speak slowly.', 1),
    (18, 1, 1, 1, '무슨 뜻이에요?', 'What does that mean?', 1),
    (19, 1, 1, 1, '도와주세요.', 'Please help me.', 1),
    (20, 1, 1, 1, '제가 도와드릴게요.', 'I will help you.', 1),

    (21, 1, 1, 1, '배가 고파요.', 'I am hungry.', 1),
    (22, 1, 1, 1, '목이 말라요.', 'I am thirsty.', 1),
    (23, 1, 1, 1, '커피 한 잔 주세요.', 'Please give me a cup of coffee.', 1),
    (24, 1, 1, 1, '물 한 잔 주세요.', 'Please give me a glass of water.', 1),
    (25, 1, 1, 1, '점심 먹었어요?', 'Did you have lunch?', 1),
    (26, 1, 1, 1, '저녁은 뭐 먹을까요?', 'What should we eat for dinner?', 1),
    (27, 1, 1, 1, '정말 맛있어요.', 'It is really delicious.', 1),
    (28, 1, 1, 1, '조금 매워요.', 'It is a little spicy.', 1),
    (29, 1, 1, 1, '배가 불러요.', 'I am full.', 1),
    (30, 1, 1, 1, '계산해 주세요.', 'Please give me the bill.', 1),

    (31, 1, 1, 1, '지금 몇 시예요?', 'What time is it now?', 1),
    (32, 1, 1, 1, '오늘은 월요일이에요.', 'Today is Monday.', 1),
    (33, 1, 1, 1, '오늘 날씨가 좋네요.', 'The weather is nice today.', 1),
    (34, 1, 1, 1, '밖에 비가 와요.', 'It is raining outside.', 1),
    (35, 1, 1, 1, '오늘은 정말 더워요.', 'It is really hot today.', 1),
    (36, 1, 1, 1, '오늘은 조금 추워요.', 'It is a little cold today.', 1),
    (37, 1, 1, 1, '우산을 가져가세요.', 'Take an umbrella with you.', 1),
    (38, 1, 1, 1, '창문을 열어 주세요.', 'Please open the window.', 1),
    (39, 1, 1, 1, '문을 닫아 주세요.', 'Please close the door.', 1),
    (40, 1, 1, 1, '불을 켜 주세요.', 'Please turn on the light.', 1),

    (41, 1, 2, 2, '오늘 아침에는 조금 늦게 일어났어요.', 'I woke up a little late this morning.', 1),
    (42, 1, 2, 2, '일어나자마자 물을 한 잔 마셨어요.', 'I drank a glass of water as soon as I woke up.', 1),
    (43, 1, 2, 2, '아침 식사를 하고 출근할 준비를 했어요.', 'I had breakfast and got ready for work.', 1),
    (44, 1, 2, 2, '오늘은 평소보다 일찍 집을 나왔어요.', 'I left home earlier than usual today.', 1),
    (45, 1, 2, 2, '버스가 늦게 와서 조금 기다렸어요.', 'The bus was late, so I waited for a while.', 1),
    (46, 1, 2, 2, '지하철에 사람이 정말 많았어요.', 'The subway was very crowded.', 1),
    (47, 1, 2, 2, '회사에 도착하자마자 컴퓨터를 켰어요.', 'I turned on my computer as soon as I arrived at work.', 1),
    (48, 1, 2, 2, '오전에 처리해야 할 일이 많아요.', 'I have a lot of work to do this morning.', 1),
    (49, 1, 2, 2, '잠깐 쉬었다가 다시 시작할게요.', 'I will take a short break and start again.', 1),
    (50, 1, 2, 2, '오늘 회의는 오후 두 시에 시작해요.', 'The meeting starts at two this afternoon.', 1),

    (51, 1, 2, 2, '점심시간에 근처 식당에 갔어요.', 'I went to a nearby restaurant during lunch.', 1),
    (52, 1, 2, 2, '오늘 점심 메뉴를 아직 정하지 못했어요.', 'I have not decided what to eat for lunch yet.', 1),
    (53, 1, 2, 2, '식사 후에 커피를 마시러 갈까요?', 'Shall we go for coffee after lunch?', 1),
    (54, 1, 2, 2, '저는 따뜻한 아메리카노를 좋아해요.', 'I like hot Americano coffee.', 1),
    (55, 1, 2, 2, '저는 설탕을 넣지 않고 커피를 마셔요.', 'I drink coffee without sugar.', 1),
    (56, 1, 2, 2, '퇴근하기 전에 이메일을 확인했어요.', 'I checked my email before leaving work.', 1),
    (57, 1, 2, 2, '오늘 해야 할 일은 거의 끝났어요.', 'I am almost finished with my work for today.', 1),
    (58, 1, 2, 2, '집에 가는 길에 장을 볼 거예요.', 'I am going grocery shopping on my way home.', 1),
    (59, 1, 2, 2, '냉장고에 우유가 거의 남지 않았어요.', 'There is almost no milk left in the refrigerator.', 1),
    (60, 1, 2, 2, '저녁에 간단한 음식을 만들어 먹었어요.', 'I made a simple meal for dinner.', 1),

    (61, 1, 2, 2, '식사를 마치고 설거지를 했어요.', 'I washed the dishes after dinner.', 1),
    (62, 1, 2, 2, '세탁기에 빨래를 넣고 돌렸어요.', 'I put the laundry in the washing machine.', 1),
    (63, 1, 2, 2, '방을 정리하니 기분이 좋아졌어요.', 'I felt better after cleaning my room.', 1),
    (64, 1, 2, 2, '오늘은 쓰레기를 버리는 날이에요.', 'Today is the day to take out the trash.', 1),
    (65, 1, 2, 2, '잠들기 전에 샤워를 할 거예요.', 'I am going to take a shower before going to bed.', 1),
    (66, 1, 2, 2, '요즘 잠이 조금 부족한 것 같아요.', 'I think I have not been getting enough sleep lately.', 1),
    (67, 1, 2, 2, '오늘은 일찍 자려고 해요.', 'I am going to bed early tonight.', 1),
    (68, 1, 2, 2, '주말에는 늦잠을 자고 싶어요.', 'I want to sleep late on the weekend.', 1),
    (69, 1, 2, 2, '시간이 있으면 산책하러 나가요.', 'I go for a walk when I have time.', 1),
    (70, 1, 2, 2, '저녁을 먹고 동네를 한 바퀴 걸었어요.', 'I took a walk around the neighborhood after dinner.', 1),

    (71, 1, 2, 2, '마트에 가서 필요한 물건을 샀어요.', 'I went to the supermarket and bought what I needed.', 1),
    (72, 1, 2, 2, '이 제품은 얼마예요?', 'How much is this product?', 1),
    (73, 1, 2, 2, '조금 더 큰 사이즈가 있나요?', 'Do you have a larger size?', 1),
    (74, 1, 2, 2, '카드로 결제할게요.', 'I will pay by card.', 1),
    (75, 1, 2, 2, '영수증도 같이 주세요.', 'Please give me the receipt as well.', 1),

    (76, 1, 3, 3, '오늘은 해야 할 일이 많아서 하루 종일 정신없이 바빴어요.', 'I had so much to do today that I was busy all day.', 1),
    (77, 1, 3, 3, '출근길에 교통이 막혀서 회사에 조금 늦게 도착했어요.', 'Traffic was heavy on my way to work, so I arrived a little late.', 1),
    (78, 1, 3, 3, '급한 일이 생겨서 원래 계획했던 일정을 변경해야 했어요.', 'Something urgent came up, so I had to change my original schedule.', 1),
    (79, 1, 3, 3, '오늘 회의에서 새로운 프로젝트에 대한 이야기를 나눴어요.', 'We discussed a new project during today''s meeting.', 1),
    (80, 1, 3, 3, '모르는 것이 있으면 혼자 고민하지 말고 바로 물어보세요.', 'If you do not know something, ask instead of worrying about it alone.', 1),

    (81, 1, 3, 3, '일이 생각보다 빨리 끝나서 조금 일찍 퇴근할 수 있었어요.', 'The work finished earlier than expected, so I was able to leave early.', 1),
    (82, 1, 3, 3, '퇴근길에 친구에게 연락해서 함께 저녁을 먹기로 했어요.', 'I contacted a friend on my way home and decided to have dinner together.', 1),
    (83, 1, 3, 3, '오랜만에 만난 친구와 이런저런 이야기를 나누며 즐거운 시간을 보냈어요.', 'I had a great time talking about many things with a friend I had not seen for a while.', 1),
    (84, 1, 3, 3, '내일 아침 일찍 일어나야 해서 오늘은 오래 놀 수 없어요.', 'I cannot stay out late today because I have to wake up early tomorrow.', 1),
    (85, 1, 3, 3, '집에 도착하면 먼저 편한 옷으로 갈아입고 조금 쉴 거예요.', 'When I get home, I will change into comfortable clothes and rest for a while.', 1),

    (86, 1, 3, 3, '요즘은 건강을 위해 가능하면 매일 조금씩 걷고 있어요.', 'These days, I try to walk a little every day for my health.', 1),
    (87, 1, 3, 3, '운동을 꾸준히 하려면 무리하지 않고 습관을 만드는 것이 중요해요.', 'To exercise regularly, it is important to build a habit without overdoing it.', 1),
    (88, 1, 3, 3, '날씨가 좋으면 집에만 있지 않고 밖으로 나가고 싶어요.', 'When the weather is nice, I want to go outside instead of staying home.', 1),
    (89, 1, 3, 3, '주말에는 밀린 집안일을 하고 남는 시간에는 푹 쉬는 편이에요.', 'On weekends, I do household chores and relax during my free time.', 1),
    (90, 1, 3, 3, '휴대폰 배터리가 거의 없어서 충전기를 찾고 있어요.', 'My phone battery is almost dead, so I am looking for a charger.', 1),

    (91, 1, 3, 3, '인터넷 연결이 불안정해서 영상이 자꾸 끊기는 것 같아요.', 'The internet connection is unstable, so the video keeps stopping.', 1),
    (92, 1, 3, 3, '필요한 파일을 찾지 못해서 컴퓨터 안을 한참 검색했어요.', 'I searched my computer for a long time because I could not find the file I needed.', 1),
    (93, 1, 3, 3, '약속 시간보다 조금 일찍 도착해서 근처 카페에서 기다리고 있어요.', 'I arrived a little early, so I am waiting at a nearby cafe.', 1),
    (94, 1, 3, 3, '길을 잘못 들어서 목적지까지 예상보다 시간이 더 걸렸어요.', 'I took the wrong road, so it took longer than expected to reach my destination.', 1),
    (95, 1, 3, 3, '모르는 길에서는 휴대폰 지도를 확인하는 것이 가장 편해요.', 'It is easiest to check a map on your phone when you do not know the way.', 1),

    (96, 1, 3, 3, '필요한 물건이 생각나면 잊어버리기 전에 메모해 두는 편이에요.', 'When I remember something I need, I usually write it down before I forget.', 1),
    (97, 1, 3, 3, '오늘 할 일을 모두 끝내고 나니 마음이 한결 편해졌어요.', 'I felt much more relaxed after finishing everything I had to do today.', 1),
    (98, 1, 3, 3, '가끔은 특별한 계획 없이 집에서 조용히 쉬는 것도 좋아요.', 'Sometimes I enjoy relaxing quietly at home without any special plans.', 1),
    (99, 1, 3, 3, '바쁜 하루였지만 해야 할 일을 끝내서 뿌듯한 기분이 들어요.', 'It was a busy day, but I feel satisfied because I finished what I had to do.', 1),
    (100, 1, 3, 3, '내일도 오늘처럼 기분 좋은 하루가 되었으면 좋겠어요.', 'I hope tomorrow will be another pleasant day like today.', 1);

COMMIT;