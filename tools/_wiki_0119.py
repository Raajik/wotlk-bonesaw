p = r'A:\obsidian\jeremy\wiki\Bonesaw.md'
add = '''

## 0.1.119 ship learnings (2026-08-30)
- Visual Studio vanished from the ship machine mid-day (vswhere finds nothing; C:/Program Files/Microsoft Visual Studio empty) so cargo cannot link msvc. build_launcher.py now falls back to a mingw-w64 cross-build inside the rust:latest container via tools/_launcher_build.cmd -- mount the REPO ROOT, not tools/launcher (build.rs reads ../client-update/Bonesaw.version). Rust build scripts compile for the host, so rustup target add gnu on the host alone does not help.
- RandomPlayerbotMgr::UpdateAIInternal re-arms at 30-60s (randomBotUpdateInterval * (onlineBotFocus+25) * 10 ms) -- anything scheduled from it needs a deadline longer than that or its own driver. LFG fill teleports now run from PlayerbotsScript::OnUpdate (every world tick) with 5s spacing.
- UnitScript OnDamage is the single funnel for ALL direct unit damage (melee + spell direct + periodic ticks); do not also hook ModifyPeriodicDamageAurasTick for the same multiplier. Destructible building damage only ever reaches OnGameObjectModifyHealth.
- Violet Hold rework: boss waves are the even ones 2..12, all six bosses release in shuffled order stored in persistent data slots 2..7, Cyanigosa = 13; cleanup wave restore = killed bosses * 2. Boss creatures carry real BOSS_* slots (BossAI); pseudo slots DATA_1ST/2ND_BOSS only mirror the first two kills.
- ship_bookkeeping.py aborts mid-tail on any failure and its bump check expects the PREVIOUS version in Bonesaw.version -- after a failed run, git reset --hard HEAD~1 the bump commit and re-run; nothing tracked changes before the launcher step.
- Line endings differ per-directory: VioletHold + src core CRLF; mod-playerbots + mod-living-gear cpp LF. Probe with count(b"\\r\\n") before python patching.
'''
with open(p, 'a', encoding='utf-8', newline='') as f:
    f.write(add)
print('wiki appended')
import os
if os.path.exists('HANDOFF.md'):
    os.remove('HANDOFF.md')
    print('HANDOFF.md deleted')
