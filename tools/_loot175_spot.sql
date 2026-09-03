SELECT 33 AS m, ct.entry, ct.name, ct.rank, cl.lootid,
  (SELECT COALESCE(SUM(LEAST(c2.Chance,100)),0) FROM creature_loot_template c2 JOIN item_template i2 ON i2.entry=c2.Item WHERE c2.Entry=cl.lootid AND i2.Quality=2) AS green_pct,
  (SELECT COUNT(*) FROM creature_loot_template c3 WHERE c3.Entry=cl.lootid) AS rows_n
FROM creature c JOIN creature_template ct ON ct.entry=c.id
LEFT JOIN creature_template cl ON cl.entry=ct.entry
WHERE c.map=33 AND ct.lootid>0 AND ct.rank IN (0,1)
GROUP BY ct.entry ORDER BY green_pct DESC LIMIT 8;
