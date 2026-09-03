"""
Find module hook overrides that the core will never call.

AzerothCore dispatches script hooks through CALL_ENABLED_HOOKS, which iterates
ONLY the scripts that named that hook in their constructor's enabled-hook list:

    ProgressionPlayer() : PlayerScript("LivingGearProgressionPlayer", {
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_GIVE_EXP,     <-- without this line, OnPlayerGiveXP
        ...                             below is dead code
    }) { }

So an `override` with no matching entry in that list compiles, reads correctly,
and never executes. Nothing warns. This is the same silent-death shape as a
perk whose id is read in a function nobody calls -- and it is mechanically
checkable, which the other failure modes are not.

    python tools/perk_hook_audit.py

The hook-name -> method-name pairing is not guessed. It is read out of the
core's own dispatchers (src/server/game/Scripting/ScriptDefines/*.cpp), where
every CALL_ENABLED_HOOKS line states the pair authoritatively.

Reports two things:
  MISSING  an override with no registration -- dead code, the bug this hunts
  UNUSED   a registration with no override -- harmless, but usually means the
           override was renamed or deleted and the list was not updated
"""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFINES = ROOT / "src" / "server" / "game" / "Scripting" / "ScriptDefines"
MODULES = ROOT / "modules"

# One dispatcher function in the core, e.g.
#   bool ScriptMgr::OnPlayerCanUseChat(Player* p, uint32 t, uint32 l, std::string& m, Channel* c)
#   { CALL_ENABLED_BOOLEAN_HOOKS(PlayerScript, PLAYERHOOK_CAN_PLAYER_USE_CHANNEL_CHAT,
#                                !script->OnPlayerCanUseChat(p, t, l, m, c)); }
#
# The enclosing signature is what disambiguates OVERLOADS, and it has to:
# OnPlayerCanUseChat exists five times over -- plain, private, group, guild and
# channel -- each paired with a different hook. Keying on the method name alone
# produced a confident false positive on the first run of this tool (it called
# the Living Gear addon dispatcher dead code, when the addon demonstrably
# works), so the pairing is (method, arity, last parameter type).
# class PerksPlayer : public PlayerScript
CLASS_RE = re.compile(r"class\s+(\w+)\s*(?:final\s*)?:\s*public\s+(\w+Script)")
DISPATCH_RE = re.compile(
    r"^\w[\w:<>*&\s]*?\bScriptMgr::(\w+)\s*\(([^)]*)\)\s*$",
    re.M,
)
CALL_RE = re.compile(
    r"CALL_ENABLED(?:_BOOLEAN)?(?:_HOOKS_WITH_DEFAULT_FALSE|_HOOKS)?\s*\(\s*"
    r"(\w+)\s*,\s*(\w+)\s*,\s*(?:!)?script->(\w+)\s*\(([^;]*?)\)\s*\)?\s*;",
    re.S,
)

def param_types(params: str) -> list[str]:
    """Parameter TYPES from a signature, comments stripped, names dropped."""
    params = re.sub(r"/\*.*?\*/", " ", params)
    out = []
    depth = 0
    cur = ""
    for ch in params:
        if ch in "<(":
            depth += 1
        elif ch in ">)":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur)
    types = []
    for raw in out:
        t = raw.strip()
        if not t:
            continue
        # drop a trailing parameter NAME, keep the type and its * / &
        t = re.sub(r"\b\w+\s*(?:=\s*[^,]+)?$", "", t).strip()
        t = re.sub(r"\s+", "", t)
        types.append(t or raw.strip())
    return types


def signature_key(method: str, params: str) -> tuple[str, int, str]:
    types = param_types(params)
    return (method, len(types), types[-1] if types else "")


def load_pairs() -> dict[tuple[str, str, int, str], str]:
    """(script type, method, arity, last param type) -> hook enum name."""
    pairs: dict[tuple[str, str, int, str], str] = {}
    if not DEFINES.is_dir():
        sys.exit(f"missing {DEFINES} -- run this from the repo with core sources present")
    for path in sorted(DEFINES.glob("*.cpp")):
        text = path.read_text(encoding="utf-8", errors="replace")
        # Walk dispatcher functions so each CALL is attributed to the signature
        # that encloses it.
        sigs = [(m.start(), m.group(1), m.group(2)) for m in DISPATCH_RE.finditer(text)]
        for i, (pos, _method, params) in enumerate(sigs):
            end = sigs[i + 1][0] if i + 1 < len(sigs) else len(text)
            for script_type, hook, called, _args in CALL_RE.findall(text[pos:end]):
                key = (script_type,) + signature_key(called, params)
                pairs[key] = hook
    return pairs


def brace_span(text: str, start: int) -> tuple[int, int]:
    """Span of the {...} body beginning at or after `start`."""
    open_at = text.find("{", start)
    if open_at < 0:
        return start, len(text)
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return open_at, i
    return open_at, len(text)


def audit_file(path: pathlib.Path, pairs: dict[tuple[str, str, int, str], str]) -> list[str]:
    text = path.read_text(encoding="utf-8", errors="replace")
    problems: list[str] = []
    for m in CLASS_RE.finditer(text):
        cls, script_type = m.group(1), m.group(2)
        known = {k[1:]: h for k, h in pairs.items() if k[0] == script_type}
        if not known:
            continue  # a script type with no enabled-hook dispatch (WorldScript etc.)
        body_start, body_end = brace_span(text, m.end())
        body = text[body_start:body_end]

        # The registration list is the brace-init block in the constructor.
        reg = set()
        ctor = re.search(re.escape(cls) + r"\s*\(\s*\)\s*:\s*" + re.escape(script_type) + r"\s*\(", body)
        if ctor:
            list_open = body.find("{", ctor.end())
            list_close = body.find("}", list_open) if list_open >= 0 else -1
            if list_open >= 0 and list_close > list_open:
                reg = set(re.findall(r"\b[A-Z][A-Z0-9_]*HOOK[A-Z0-9_]*\b", body[list_open:list_close]))
        if not ctor:
            continue  # not the constructor-list form; nothing to compare

        overrides = {}
        for om in re.finditer(
            r"^\s*(?:\[\[nodiscard\]\]\s*)?[\w:<>&*\s]+?\b(\w+)\s*\(([^;{]*)\)\s*(?:const\s*)?override\b",
            body, re.M):
            overrides[signature_key(om.group(1), om.group(2))] = om.group(1)

        for sig, method in sorted(overrides.items(), key=lambda kv: kv[1]):
            hook = known.get(sig)
            if hook is None:
                # No dispatcher pair for this exact signature. Either a script
                # type without enabled-hook dispatch, or a signature this tool
                # failed to normalise -- say so rather than guessing "dead".
                by_name = [h for k, h in known.items() if k[0] == sig[0]]
                if by_name:
                    problems.append(
                        f"UNMATCHED {path.relative_to(ROOT)}  {cls}::{method} "
                        f"-- overload not matched to a hook; checked by hand: {sorted(set(by_name))}"
                    )
                continue
            if hook not in reg:
                problems.append(
                    f"MISSING  {path.relative_to(ROOT)}  {cls}::{method} "
                    f"-- override with no {hook} in the enabled-hook list; never called"
                )
        used = set()
        for sig in overrides:
            hook = known.get(sig)
            if hook:
                used.add(hook)
        for hook in sorted(reg - used):
            problems.append(
                f"UNUSED   {path.relative_to(ROOT)}  {cls} registers {hook} with no matching override"
            )
    return problems


def main() -> int:
    pairs = load_pairs()
    print(f"Hook dispatch pairs read from the core: {len(pairs)} "
          f"(keyed by method + arity + last parameter type, so overloads stay distinct)")

    files = sorted(MODULES.glob("mod-*/src/**/*.cpp"))
    missing = 0
    unused = 0
    for path in files:
        for line in audit_file(path, pairs):
            print("  " + line)
            if line.startswith("MISSING"):
                missing += 1
            elif line.startswith("UNUSED"):
                unused += 1

    print(f"\nscanned {len(files)} module source files")
    print(f"  dead overrides (MISSING) : {missing}")
    print(f"  stale registrations      : {unused}")
    if missing:
        print("\nA MISSING line is a hook the core will never call. The code reads")
        print("correctly and does nothing, which is the hardest kind of bug to see.")
    return 1 if missing else 0


if __name__ == "__main__":
    raise SystemExit(main())
