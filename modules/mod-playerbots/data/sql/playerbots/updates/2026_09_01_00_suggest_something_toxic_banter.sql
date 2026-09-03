-- Report #250: more bot banter in the suggest_something_toxic pool.
-- The pool lives in ai_playerbot_texts; SuggestWhatToDoAction picks a line and
-- fills %random_inventory_item_link with a link from the bot's own bags, which
-- is what produced the beloved "I want to shove [Reservoir Key] up my ass".
-- Twenty new stupid lines, same flavor: immature, harmless, no slurs.
INSERT INTO ai_playerbot_texts (id, name, text, say_type, reply_type, text_loc1, text_loc2, text_loc3, text_loc4, text_loc5, text_loc6, text_loc7, text_loc8) VALUES
(2150, 'suggest_something_toxic', 'i licked %random_inventory_item_link and now my tongue is numb', 0, 0, '', '', '', '', '', '', '', ''),
(2151, 'suggest_something_toxic', '%random_inventory_item_link is inside my walls', 0, 0, '', '', '', '', '', '', '', ''),
(2152, 'suggest_something_toxic', 'i keep %random_inventory_item_link under my pillow for luck', 0, 0, '', '', '', '', '', '', '', ''),
(2153, 'suggest_something_toxic', 'my doctor said no more %random_inventory_item_link but i do not listen', 0, 0, '', '', '', '', '', '', '', ''),
(2154, 'suggest_something_toxic', 'i smell like %random_inventory_item_link and i am proud of it', 0, 0, '', '', '', '', '', '', '', ''),
(2155, 'suggest_something_toxic', 'i named my pet %random_inventory_item_link', 0, 0, '', '', '', '', '', '', '', ''),
(2156, 'suggest_something_toxic', 'i have been eating %random_inventory_item_link for breakfast, lunch and dinner', 0, 0, '', '', '', '', '', '', '', ''),
(2157, 'suggest_something_toxic', '%random_inventory_item_link fixed my marriage, ruined two others', 0, 0, '', '', '', '', '', '', '', ''),
(2158, 'suggest_something_toxic', 'i dreamt about %random_inventory_item_link last night, do not ask', 0, 0, '', '', '', '', '', '', '', ''),
(2159, 'suggest_something_toxic', 'i hide %random_inventory_item_link in my boots', 0, 0, '', '', '', '', '', '', '', ''),
(2160, 'suggest_something_toxic', '%random_inventory_item_link? i hardly know her!', 0, 0, '', '', '', '', '', '', '', ''),
(2161, 'suggest_something_toxic', 'i traded my sister for %random_inventory_item_link, no regrets', 0, 0, '', '', '', '', '', '', '', ''),
(2162, 'suggest_something_toxic', 'the voices told me to buy more %random_inventory_item_link', 0, 0, '', '', '', '', '', '', '', ''),
(2163, 'suggest_something_toxic', 'i am eighty percent %random_inventory_item_link at this point', 0, 0, '', '', '', '', '', '', '', ''),
(2164, 'suggest_something_toxic', 'do not tell the guard, but %random_inventory_item_link is in my pants', 0, 0, '', '', '', '', '', '', '', ''),
(2165, 'suggest_something_toxic', 'i owe the bank three stacks of %random_inventory_item_link', 0, 0, '', '', '', '', '', '', '', ''),
(2166, 'suggest_something_toxic', '%random_inventory_item_link tastes like better decisions than mine', 0, 0, '', '', '', '', '', '', '', ''),
(2167, 'suggest_something_toxic', 'i teach my children about %random_inventory_item_link', 0, 0, '', '', '', '', '', '', '', ''),
(2168, 'suggest_something_toxic', 'grandpa died holding %random_inventory_item_link, what a legend', 0, 0, '', '', '', '', '', '', '', ''),
(2169, 'suggest_something_toxic', 'i am legally not allowed within ten yards of %random_inventory_item_link', 0, 0, '', '', '', '', '', '', '', '');
