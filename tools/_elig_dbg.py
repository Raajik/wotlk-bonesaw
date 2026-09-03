"""Find the REAL #127 hole: Tumultuous Necklace secondaries not leveling.

Math flow for a suffix-only item (Tumultuous, entry 51996, ilvl 35,
RandomSuffix 381 -> ItemRandomSuffix 71 'of the Bandit'):
  GrownStats: base = ReadBaseStats(proto)  [primaries all 0, armor 0]
            + ReadSuffixStats(item)      [agi/sta/AP from suffix]
  mult = StatBudgetFor(NECK, effIlvl) / StatBudgetFor(NECK, 35)
  base.sec *= mult  -> suffix secondaries DO scale in GrownStats.
  WornDelta = grown - base(=same reads)  -> correct.

So where does it break? ReadSuffixStats requires `item` (the instance):
  - GrownStats(proto, st, item) is called with item from SendLivingItem /
    ReapplyWorn / WornDelta paths. Check BankAttunement (line 1031):
    GrownStats(proto, st) -- NO ITEM. Suffix stats missing there.
  - Also AddItemXpAndBank level-ups? And: ReadSuffixStats(item) reads
    ITEM_FIELD_PROPERTY_SEED (suffixFactor) from the instance.

KEY SUSPECT: suffixFactor. The client-server seed for 51996 was generated
at ilvl 35 = Rare factor 8 (see _suffix math). agi = 5259*8/10000 = 4.
So the necklace shows +4 agi/+4 sta/+8 AP. When the item LEVELS, the
suffix amount stays keyed to the ORIGINAL factor (8) -- the ilvl-35
factor -- while primaries scale by the budget ratio. Suffix secondaries
scale by the same mult (they're in base.sec), so they DO grow.

Print the IsEligible + budget interplay for NECK: does the neck slot have
budget rows? It should. So maybe the true bug is elsewhere: the level-up
writer. ReadSuffixStats is only valid when `item` is passed; AddItemXpAndBank
and BankAttunement call GrownStats WITHOUT item -> secondaries read 0 ->
banked absorb rows for suffix-only items store ZEROS -> the ATT contribution
for that item is zero -> 'secondaries never level' in the attune panel.
"""
print("analysis only")
