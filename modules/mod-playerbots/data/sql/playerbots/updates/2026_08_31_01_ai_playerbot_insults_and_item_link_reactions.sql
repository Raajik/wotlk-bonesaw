-- #########################################################
-- Playerbots - more insults + item link reactions (report #229)
--
-- Viria: "Need even more playerbot insults to counter the
-- defaults" and wanting bots to react to item links other
-- players post ("wow, i just got [link]").
--
-- Two halves:
--   * content -- 50 new suggest_something_toxic lines (the
--     broadcast send path fills no name placeholders, so these
--     use none) and 25 new taunt lines (<target> only, which is
--     what the say::taunt action fills);
--   * wiring -- the chat reply engine treated a posted item link
--     like any other unmatched text and answered with the
--     not-understand pool. The new reply_item_link pool is picked
--     by ChatReplyAction::HandleItemLinkReply instead (see
--     core-patch 0060 in mod-living-gear). The receive-side
--     chance gates in PlayerbotAI already cap how many bots
--     answer, so this pool needs no ai_playerbot_texts_chance
--     row -- adding one would do nothing on this code path.
--
-- Applied by the worldserver's playerbots DB updater into
-- acore_playerbots (updates_include lists
-- $/data/sql/playerbots/updates) -- NOT by ac-db-import.
-- #########################################################

-- taunt additions
DELETE FROM ai_playerbot_texts WHERE name = 'taunt' AND text IN (
'<target>, thats a paddlin',
'you mess with the bull <target>, you get the horns',
'<target> is about to learn some manners',
'ill glass <target> twice for free',
'<target> hits like a wet noodle',
'run <target>, i need the cardio... actually no',
'<target>, your cooldowns called. they quit',
'<target> is loot on legs',
'oi <target>, is that all you got',
'<target>, you fight like a training dummy',
'the only thing you kill <target> is time',
'<target>, go back to the starting zone',
'even the elites should be embarrassed, <target>',
'i have heard better threats from murlocs, <target>',
'you swung at me <target>? bless your heart',
'<target>, thats coming out of your hides',
'keep swinging <target>, im getting comfy',
'<target>, you made me waste a potion. rude',
'ill send you to the graveyard express, <target>',
'<target> is why the queue times are long',
'you call that a crit, <target>?',
'<target>, bring a broom next time',
'one <target> down, zero lessons learned',
'<target> fights like the mobs owe them money',
'your mama plays horde, <target>'
);

INSERT INTO ai_playerbot_texts (name, text, say_type, reply_type) VALUES
('taunt', '<target>, thats a paddlin', 0, 0),
('taunt', 'you mess with the bull <target>, you get the horns', 0, 0),
('taunt', '<target> is about to learn some manners', 0, 0),
('taunt', 'ill glass <target> twice for free', 0, 0),
('taunt', '<target> hits like a wet noodle', 0, 0),
('taunt', 'run <target>, i need the cardio... actually no', 0, 0),
('taunt', '<target>, your cooldowns called. they quit', 0, 0),
('taunt', '<target> is loot on legs', 0, 0),
('taunt', 'oi <target>, is that all you got', 0, 0),
('taunt', '<target>, you fight like a training dummy', 0, 0),
('taunt', 'the only thing you kill <target> is time', 0, 0),
('taunt', '<target>, go back to the starting zone', 0, 0),
('taunt', 'even the elites should be embarrassed, <target>', 0, 0),
('taunt', 'i have heard better threats from murlocs, <target>', 0, 0),
('taunt', 'you swung at me <target>? bless your heart', 0, 0),
('taunt', '<target>, thats coming out of your hides', 0, 0),
('taunt', 'keep swinging <target>, im getting comfy', 0, 0),
('taunt', '<target>, you made me waste a potion. rude', 0, 0),
('taunt', 'ill send you to the graveyard express, <target>', 0, 0),
('taunt', '<target> is why the queue times are long', 0, 0),
('taunt', 'you call that a crit, <target>?', 0, 0),
('taunt', '<target>, bring a broom next time', 0, 0),
('taunt', 'one <target> down, zero lessons learned', 0, 0),
('taunt', '<target> fights like the mobs owe them money', 0, 0),
('taunt', 'your mama plays horde, <target>', 0, 0);

-- suggest_something_toxic additions
DELETE FROM ai_playerbot_texts WHERE name = 'suggest_something_toxic' AND text IN (
'im not toxic, im accurate',
'skill issue',
'that was all me and you know it',
'im the main character here',
'you died to that?',
'i could do this fight asleep',
'bots play better than that',
'take the L and log off',
'thats a you problem',
'i dont carry dead weight',
'you pull like you read the guide upside down',
'your spec is a cry for help',
'did you spec that in the dark?',
'that rotation needs a medic',
'you bring the wipes, i bring the kills',
'stand in fire some more, its funny',
'the floor is not a mechanic',
'rez me and act like it never happened',
'you aggro like you owe the mobs money',
'thats gonna be a yikes from me',
'the boss died of boredom waiting on you',
'i have seen NPCs with better awareness',
'grats on the carry you did not earn',
'my pet tanks better than that',
'you made that boss look easy. for the boss',
'top of the meters, where i belong',
'loot rules are simple, i win',
'you did your best and your best is rough',
'thats why they made easy mode for you',
'keep talking, im still outdamaging you',
'your gear weeps',
'read a tooltip sometime',
'impressive how you find every fire',
'you tank like a soggy loaf of bread',
'delete the addons, the problem is you',
'facts dont care about your parse',
'you bring vibes, i bring kills',
'the raid leader quietly regrets inviting you',
'the dungeon cleared itself out of spite',
'log off and let the adults queue',
'i heal faster than you die. barely',
'this is my world, you just live in it',
'imagine needing that many heals',
'my grandma outdps you on a mousepad',
'you fight like a mana starved mage',
'the only thing you carry is lag',
'i only pull for the drama',
'thats coming out of your repair bill',
'you pull like a laggy mage. no offense to mages',
'even the training dummy feels bad for you'
);

INSERT INTO ai_playerbot_texts (name, text, say_type, reply_type) VALUES
('suggest_something_toxic', 'im not toxic, im accurate', 0, 0),
('suggest_something_toxic', 'skill issue', 0, 0),
('suggest_something_toxic', 'that was all me and you know it', 0, 0),
('suggest_something_toxic', 'im the main character here', 0, 0),
('suggest_something_toxic', 'you died to that?', 0, 0),
('suggest_something_toxic', 'i could do this fight asleep', 0, 0),
('suggest_something_toxic', 'bots play better than that', 0, 0),
('suggest_something_toxic', 'take the L and log off', 0, 0),
('suggest_something_toxic', 'thats a you problem', 0, 0),
('suggest_something_toxic', 'i dont carry dead weight', 0, 0),
('suggest_something_toxic', 'you pull like you read the guide upside down', 0, 0),
('suggest_something_toxic', 'your spec is a cry for help', 0, 0),
('suggest_something_toxic', 'did you spec that in the dark?', 0, 0),
('suggest_something_toxic', 'that rotation needs a medic', 0, 0),
('suggest_something_toxic', 'you bring the wipes, i bring the kills', 0, 0),
('suggest_something_toxic', 'stand in fire some more, its funny', 0, 0),
('suggest_something_toxic', 'the floor is not a mechanic', 0, 0),
('suggest_something_toxic', 'rez me and act like it never happened', 0, 0),
('suggest_something_toxic', 'you aggro like you owe the mobs money', 0, 0),
('suggest_something_toxic', 'thats gonna be a yikes from me', 0, 0),
('suggest_something_toxic', 'the boss died of boredom waiting on you', 0, 0),
('suggest_something_toxic', 'i have seen NPCs with better awareness', 0, 0),
('suggest_something_toxic', 'grats on the carry you did not earn', 0, 0),
('suggest_something_toxic', 'my pet tanks better than that', 0, 0),
('suggest_something_toxic', 'you made that boss look easy. for the boss', 0, 0),
('suggest_something_toxic', 'top of the meters, where i belong', 0, 0),
('suggest_something_toxic', 'loot rules are simple, i win', 0, 0),
('suggest_something_toxic', 'you did your best and your best is rough', 0, 0),
('suggest_something_toxic', 'thats why they made easy mode for you', 0, 0),
('suggest_something_toxic', 'keep talking, im still outdamaging you', 0, 0),
('suggest_something_toxic', 'your gear weeps', 0, 0),
('suggest_something_toxic', 'read a tooltip sometime', 0, 0),
('suggest_something_toxic', 'impressive how you find every fire', 0, 0),
('suggest_something_toxic', 'you tank like a soggy loaf of bread', 0, 0),
('suggest_something_toxic', 'delete the addons, the problem is you', 0, 0),
('suggest_something_toxic', 'facts dont care about your parse', 0, 0),
('suggest_something_toxic', 'you bring vibes, i bring kills', 0, 0),
('suggest_something_toxic', 'the raid leader quietly regrets inviting you', 0, 0),
('suggest_something_toxic', 'the dungeon cleared itself out of spite', 0, 0),
('suggest_something_toxic', 'log off and let the adults queue', 0, 0),
('suggest_something_toxic', 'i heal faster than you die. barely', 0, 0),
('suggest_something_toxic', 'this is my world, you just live in it', 0, 0),
('suggest_something_toxic', 'imagine needing that many heals', 0, 0),
('suggest_something_toxic', 'my grandma outdps you on a mousepad', 0, 0),
('suggest_something_toxic', 'you fight like a mana starved mage', 0, 0),
('suggest_something_toxic', 'the only thing you carry is lag', 0, 0),
('suggest_something_toxic', 'i only pull for the drama', 0, 0),
('suggest_something_toxic', 'thats coming out of your repair bill', 0, 0),
('suggest_something_toxic', 'you pull like a laggy mage. no offense to mages', 0, 0),
('suggest_something_toxic', 'even the training dummy feels bad for you', 0, 0);

-- reply_item_link: NEW pool, reactions to item links posted in
-- chat (%s = the poster, %random_inventory_item_link is a
-- bot-owned item so lines can brag back)
DELETE FROM ai_playerbot_texts WHERE name = 'reply_item_link' AND text IN (
'wow %s grats',
'i want one of those %s',
'lucky %s, very lucky',
'grats %s, dont vendor it',
'where does that even drop, %s?',
'nice one %s, show off',
'one day i will get one of those too, %s',
'%s, save that for me when you outgrow it',
'thats the good stuff %s',
'ooh %s, link it again, i did not get a good look',
'grats %s, i am pretending not to be jealous',
'you found that where, %s?',
'that reminds me of my %random_inventory_item_link',
'i have one of those... %random_inventory_item_link, close enough'
);

INSERT INTO ai_playerbot_texts (name, text, say_type, reply_type) VALUES
('reply_item_link', 'wow %s grats', 0, 0),
('reply_item_link', 'i want one of those %s', 0, 0),
('reply_item_link', 'lucky %s, very lucky', 0, 0),
('reply_item_link', 'grats %s, dont vendor it', 0, 0),
('reply_item_link', 'where does that even drop, %s?', 0, 0),
('reply_item_link', 'nice one %s, show off', 0, 0),
('reply_item_link', 'one day i will get one of those too, %s', 0, 0),
('reply_item_link', '%s, save that for me when you outgrow it', 0, 0),
('reply_item_link', 'thats the good stuff %s', 0, 0),
('reply_item_link', 'ooh %s, link it again, i did not get a good look', 0, 0),
('reply_item_link', 'grats %s, i am pretending not to be jealous', 0, 0),
('reply_item_link', 'you found that where, %s?', 0, 0),
('reply_item_link', 'that reminds me of my %random_inventory_item_link', 0, 0),
('reply_item_link', 'i have one of those... %random_inventory_item_link, close enough', 0, 0);
