#!/usr/bin/env python3
"""Assert every account-perk grant site consults the purchase buy-back.

There is no single chokepoint for granting a perk in this module. UnlockPerk is
defined six separate times, once per file with a different signature each time,
and LivingGear_Vault.cpp grants Autoloot with a seventh hand-rolled INSERT. That
duplication is the same structural cause behind the class-perk audit finding
that every dead perk had its own bespoke trigger.

A grant site that does not call LivingGear_RefundIfPurchased is a site where an
account can pay for a rank, meet its unlock condition afterwards, and never get
the points back. It reads perfectly and fails silently, which is exactly the
failure mode this module keeps producing.

Exit status is 1 when a grant site is missing the call, so this can gate CI.

    python modules/mod-living-gear/tools/perk_grant_audit.py
"""

import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "src"

# The write that defines a grant site. Anything performing this INSERT is
# putting a perk on an account and therefore has to consider the buy-back.
GRANT = re.compile(r"INSERT IGNORE INTO `lg_account_perk`")
REFUND = re.compile(r"\bLivingGear_RefundIfPurchased\s*\(\s*player\b|\bRefundIfPurchased\s*\(\s*player\b")

# Editor and build leftovers sit next to the real sources in this directory.
IGNORED = re.compile(
    r"\.(bak|backup|off|built|next|deduped|dedupe-test|wip|stub|onepass|"
    r"keep|ship|exclusive|restore)|\.cpp\.[a-z0-9-]+$"
)


def main() -> int:
    if not SRC.is_dir():
        print(f"no such directory: {SRC}", file=sys.stderr)
        return 2

    failures = []
    checked = 0

    for path in sorted(SRC.glob("LivingGear*.cpp")):
        if IGNORED.search(path.name):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        grants = len(GRANT.findall(text))
        if not grants:
            continue
        checked += 1
        refunds = len(REFUND.findall(text))
        status = "ok" if refunds else "MISSING"
        print(f"  {path.name:<32} grants={grants}  refund-calls={refunds}  {status}")
        if not refunds:
            failures.append(path.name)

    print()
    if not checked:
        print("no grant sites found at all -- has lg_account_perk been renamed?")
        return 1
    if failures:
        print(f"FAIL: {len(failures)} grant site(s) never call LivingGear_RefundIfPurchased:")
        for name in failures:
            print(f"  - {name}")
        print()
        print("An account that bought that rank and later met its unlock condition")
        print("keeps paying for something it earned. Add the call beside the grant.")
        return 1

    print(f"OK: all {checked} account-perk grant sites consult the buy-back.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
