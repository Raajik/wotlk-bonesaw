-- Feature #235: remove the flying mount blocker from Wintergrasp, as was
-- done for Dalaran (rev_dalaran_flight.sql). The whole gate was the
-- "Flightless" Wintergrasp debuff (spell 58730) applied purely through
-- spell_area rows for the Wintergrasp area tree -- with the rows gone
-- there is nothing to apply and nothing to dismount flyers.
--
-- The Wintergrasp area rows carry no AREA_FLAG_NO_FLY_ZONE in either the
-- server or client AreaTable (verified against the client base DBC: 18
-- WG-tree rows, zero flagged), so unlike Dalaran no AreaTable correction
-- and no client patch are needed. The battlefield's own gate
-- (Battlefield::CanFlyIn, Spell.cpp CheckCast) still grounds flyers while
-- a Wintergrasp war is active, on purpose -- sieges stay ground/air-defense
-- fights; outside battle the zone flies like anywhere else in Northrend.

DELETE FROM `spell_area` WHERE `spell` = 58730;
