SELECT t.map,
       COUNT(*) AS mobs,
       ROUND(AVG(t.green),1) AS avg_green_per_mob_pct,
       ROUND(AVG(t.green_grp),1) AS avg_grouped_green_pct,
       ROUND(AVG(t.grey),1) AS avg_grey_per_mob_pct,
       ROUND(MAX(t.green),0) AS max_green
FROM (
  SELECT c.map AS map, ct.entry,
         COALESCE(SUM(CASE WHEN it.Quality=2 AND clt.GroupId=0 THEN LEAST(clt.Chance,100) ELSE 0 END),0) AS green,
         COALESCE(SUM(CASE WHEN it.Quality=2 AND clt.GroupId>0 THEN LEAST(clt.Chance,100) ELSE 0 END),0) AS green_grp,
         COALESCE(SUM(CASE WHEN it.Quality=0 AND clt.GroupId=0 THEN LEAST(clt.Chance,100) ELSE 0 END),0) AS grey
  FROM acore_world.creature c
  JOIN acore_world.creature_template ct ON ct.entry=c.id AND ct.lootid>0 AND ct.rank IN (0,1)
  LEFT JOIN acore_world.creature_loot_template clt ON clt.Entry=ct.lootid
  LEFT JOIN acore_world.item_template it ON it.entry=clt.Item
  WHERE c.map IN (543,574,542,576,631,33,389,530,571)
  GROUP BY c.map, ct.entry
) t
GROUP BY t.map;
