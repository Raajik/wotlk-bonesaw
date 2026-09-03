#!/usr/bin/env python3
"""Extract what is recoverable from Synastria's WoWExt client-extension files.

    python wowext_dump.py "B:/Games/WoW 3.3.5/Synastria" -o out/wowext

WoWExt.dll registers a spare packet handler so the server can patch client DBCs
at runtime -- inventing spells, items, item sets and extended costs without
shipping a DBC edit -- and caches what it receives in WoWExt*.bin. The formats
are undocumented; this decodes the parts that are solved and reports honestly on
the parts that are not.

Solved
  WoWExt.bin        fully unpacked. Container is
                        u32 magic 0x2838c53a, u32 version, u32 blob count
                        per blob: u32 key, u8 md5[16], u32 length, u8 body[]
                    and each body is an LZ4 block, decompressed into a 64 KB
                    buffer. Layout and the md5 check were read out of the
                    writer at 0x10044f70 and the decompressor at 0x100343e0.
                    63 blobs unpack to ~1 MB, of which ~750 KB is the server's
                    entire custom client-side Lua source.
  WoWExt.dll        RTTI names for all 58 server-side mod types, and the
                    Custom_* Lua API the server adds for its own addons.
  WoWExt2.bin       item name table, one record per item
  WoWExt3.bin       plaintext Lua config, already readable
  WoWExtObjLoc.bin  24-byte header plus a string table of tracked object,
                    creature and quest names

Partly solved
  WoWExt.bin        the ~10 non-Lua blobs are DBC mod streams. Their strings
                    are cleanly recovered (u16 length prefix, no terminator,
                    followed by f32 parameter tuples), but the record framing
                    around them is not fully identified, so those are dumped
                    as raw bytes plus extracted strings.

Unsolved
  WoWExtObjLoc.bin  the record area after the string table is timestamped and
                    delta-coded at roughly 6.4 bytes per record.
  WoWExtItemLoc.bin high-entropy bitset, almost certainly the client-side
                    "items you have acquired" cache behind Custom_IsHaveItem /
                    Custom_CacheHaveItems. No feature data in it.
"""

import argparse
import collections
import json
import math
import os
import re
import struct
import sys

PRINTABLE = re.compile(rb"[ -~]{4,}")


def entropy(buf):
    if not buf:
        return 0.0
    c = collections.Counter(buf)
    n = len(buf)
    return -sum(v / n * math.log2(v / n) for v in c.values())


# --------------------------------------------------------------------- dll --

def dump_dll(path):
    """The DLL's RTTI and export strings are the server's feature inventory."""
    with open(path, "rb") as fh:
        d = fh.read()
    mods = sorted({m.group(1).decode() for m in re.finditer(rb"\.\?AU(YYY_[A-Za-z0-9_]+)@@", d)})
    dbcs = sorted({m.group(1).decode()
                   for m in re.finditer(rb"WoWClientDB_Mod@U([A-Za-z0-9_]+Rec)@@", d)})
    lua = []
    seen = set()
    for m in re.finditer(rb"(Custom_[A-Za-z0-9_]+)", d):
        name = m.group(1).decode()
        if name not in seen:
            seen.add(name)
            lua.append(name)
    return {"mod_types": mods, "patched_dbcs": dbcs, "lua_api": lua}


# ------------------------------------------------------------------- ext.bin --

MAGIC_EXT = 0x2838C53A
LZ4_DEST_CAP = 0x10000  # the DLL's static output buffer at 0x10098c00


def lz4_block(src, cap=LZ4_DEST_CAP):
    """Stock LZ4 block decompression -- the exact routine at sub_100343e0."""
    out = bytearray()
    i, n = 0, len(src)
    while i < n:
        token = src[i]
        i += 1
        lit = token >> 4
        if lit == 15:
            while True:
                b = src[i]
                i += 1
                lit += b
                if b != 255:
                    break
        out += src[i:i + lit]
        i += lit
        if i >= n:
            break  # last sequence is literals only
        offset = struct.unpack_from("<H", src, i)[0]
        i += 2
        match = (token & 0xF) + 4
        if (token & 0xF) == 15:
            while True:
                b = src[i]
                i += 1
                match += b
                if b != 255:
                    break
        if offset == 0 or offset > len(out):
            raise ValueError("bad match offset %d at input byte %d" % (offset, i))
        start = len(out) - offset
        for k in range(match):
            out.append(out[start + k])  # byte-at-a-time: matches may overlap
        if len(out) > cap:
            raise ValueError("output exceeds %d byte buffer" % cap)
    return bytes(out)


def dump_ext(path):
    """Walk the blob container and LZ4-decompress every payload."""
    with open(path, "rb") as fh:
        d = fh.read()
    magic, version, count = struct.unpack_from("<3I", d, 0)
    if magic != MAGIC_EXT:
        raise ValueError("bad magic 0x%08x" % magic)
    blobs, p = [], 12
    while p + 24 <= len(d):
        key = struct.unpack_from("<I", d, p)[0]
        md5 = d[p + 4:p + 20].hex()
        length = struct.unpack_from("<I", d, p + 20)[0]
        body = d[p + 24:p + 24 + length]
        p += 24 + length
        rec = {"key": key, "md5": md5, "packed": length}
        try:
            out = lz4_block(body)
            rec["unpacked"] = len(out)
            rec["data"] = out
        except (ValueError, IndexError, struct.error) as exc:
            rec["error"] = str(exc)
        blobs.append(rec)
    return {
        "magic": "0x%08x" % magic,
        "version": version,
        "declared_blobs": count,
        "blobs": blobs,
        "leftover": len(d) - p,
    }


def is_lua(buf):
    """Lua blobs decompress to pure text; mod streams do not."""
    if not buf:
        return False
    good = sum(1 for c in buf if 32 <= c < 127 or c in (9, 10, 13))
    return good / len(buf) > 0.97


def mod_strings(buf, min_len=3):
    """Pull the u16-length-prefixed strings out of a DBC mod stream.

    Every string in these blobs is a u16 little-endian length followed by
    exactly that many bytes and no terminator -- verified against the spell
    descriptions, whose declared lengths land exactly on the following f32
    parameter tuples. Scanning for that shape recovers the custom spell, perk
    and talent names and tooltips without needing the record framing."""
    out, i, n = [], 0, len(buf)
    while i + 2 <= n:
        ln = struct.unpack_from("<H", buf, i)[0]
        if min_len <= ln <= 4096 and i + 2 + ln <= n:
            s = buf[i + 2:i + 2 + ln]
            if all(32 <= c < 127 or c in (9, 10, 13) for c in s):
                out.append(s.decode("ascii"))
                i += 2 + ln
                continue
        i += 1
    return out


# ------------------------------------------------------------------ ext2.bin --

def dump_ext2(path):
    """Item name table. Records are framed by a 0x3b tag byte, then a one-byte
    field code, then a length-prefixed name, then a fixed per-item payload whose
    fields are not yet identified."""
    with open(path, "rb") as fh:
        d = fh.read()
    items, i, n = [], 0, len(d)
    while i < n - 3:
        if d[i] == 0x3B:
            code, ln = d[i + 1], d[i + 2]
            s = d[i + 3:i + 3 + ln]
            if 0 < ln < 64 and len(s) == ln and all(32 <= ch < 127 for ch in s):
                items.append({"offset": i, "code": code, "name": s.decode()})
                i += 3 + ln
                continue
        i += 1
    return items


# --------------------------------------------------------------- objloc.bin --

def dump_objloc(path):
    """Header plus the name string table. The record area after it is
    timestamped and delta-coded; not decoded."""
    with open(path, "rb") as fh:
        d = fh.read()
    hdr = struct.unpack_from("<7I", d, 0)
    table_end = 28 + hdr[6]
    names, p = [], 28
    while p < min(table_end, len(d)):
        e = d.find(b"\0", p)
        if e < 0 or e >= table_end:
            break
        s = d[p:e]
        p = e + 1
        if s:
            names.append(s.decode("utf-8", "replace"))
    return {
        "header": list(hdr),
        "string_table_bytes": hdr[6],
        "names": names,
        "record_area_bytes": len(d) - table_end,
        "record_area_head": d[table_end:table_end + 64].hex(" "),
    }


# ------------------------------------------------------------------- report --

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("client_dir", help="a Synastria client directory containing WoWExt*.bin")
    ap.add_argument("-o", "--out", default="wowext", help="output path prefix")
    args = ap.parse_args()

    root = args.client_dir
    out_dir = os.path.dirname(os.path.abspath(args.out))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    result = {}

    def p(name):
        return os.path.join(root, name)

    if os.path.exists(p("WoWExt.dll")):
        result["dll"] = dump_dll(p("WoWExt.dll"))
    if os.path.exists(p("WoWExt.bin")):
        result["ext"] = dump_ext(p("WoWExt.bin"))
    if os.path.exists(p("WoWExt2.bin")):
        result["items"] = dump_ext2(p("WoWExt2.bin"))
    if os.path.exists(p("WoWExtObjLoc.bin")):
        result["objloc"] = dump_objloc(p("WoWExtObjLoc.bin"))
    if os.path.exists(p("WoWExt3.bin")):
        with open(p("WoWExt3.bin"), "r", encoding="utf-8", errors="replace") as fh:
            result["lua_config"] = fh.read()
    if not result:
        sys.exit("no WoWExt files found in %s" % root)

    base = os.path.basename(args.out)

    # Blob payloads go to their own files: Lua as .lua so they are greppable and
    # syntax-highlightable, mod streams as .bin next to a .txt of their strings.
    ext = result.get("ext")
    if ext:
        lua_dir, mod_dir = args.out + "_lua", args.out + "_mods"
        for b in ext["blobs"]:
            if "data" not in b:
                continue
            if is_lua(b["data"]):
                os.makedirs(lua_dir, exist_ok=True)
                with open(os.path.join(lua_dir, "blob_%02d.lua" % b["key"]),
                          "wb") as fh:
                    fh.write(b["data"])
            else:
                os.makedirs(mod_dir, exist_ok=True)
                stem = os.path.join(mod_dir, "blob_%02d" % b["key"])
                with open(stem + ".bin", "wb") as fh:
                    fh.write(b["data"])
                with open(stem + ".txt", "w", encoding="utf-8") as fh:
                    fh.write("\n".join(mod_strings(b["data"])))

    with open(args.out + ".json", "w", encoding="utf-8") as fh:
        json.dump(result, fh, indent=1, ensure_ascii=False,
                  default=lambda o: "<%d bytes>" % len(o))

    with open(args.out + ".md", "w", encoding="utf-8") as fh:
        w = fh.write
        w("# WoWExt extraction\n\nSource: `%s`\n\n" % root)

        dll = result.get("dll")
        if dll:
            w("## Feature inventory (from WoWExt.dll RTTI)\n\n")
            w("The server can push any of these %d mod types at runtime.\n"
              "Read this as the list of things they can do to a client without\n"
              "shipping a patch.\n\n" % len(dll["mod_types"]))
            for m in dll["mod_types"]:
                w("- `%s`\n" % m)
            w("\n### Client DBCs patched in memory\n\n")
            for x in dll["patched_dbcs"]:
                w("- `%s`\n" % x)
            w("\n### Lua API added for their own addons (%d functions)\n\n"
              % len(dll["lua_api"]))
            for x in dll["lua_api"]:
                w("- `%s`\n" % x)
            w("\n")

        items = result.get("items")
        if items:
            w("## Item name table -- WoWExt2.bin (%d records)\n\n" % len(items))
            codes = collections.Counter(i["code"] for i in items)
            w("Field-code spread: %s\n\n"
              % ", ".join("%d=%d" % kv for kv in sorted(codes.items())))
            w("| offset | code | name |\n|---|---|---|\n")
            for it in items:
                w("| %d | %d | %s |\n" % (it["offset"], it["code"], it["name"]))
            w("\n")

        obj = result.get("objloc")
        if obj:
            w("## Tracked object names -- WoWExtObjLoc.bin (%d names)\n\n"
              % len(obj["names"]))
            w("Header: `%s`. Record area after the table is %d bytes, "
              "timestamped and delta-coded, not decoded.\n\n"
              % (obj["header"], obj["record_area_bytes"]))
            for n in obj["names"]:
                w("- %s\n" % n)
            w("\n")

        ext = result.get("ext")
        if ext:
            w("## WoWExt.bin -- %d LZ4 blobs\n\n" % len(ext["blobs"]))
            w("Magic `%s`, version %s, %s blobs declared, %d trailing bytes.\n\n"
              % (ext["magic"], ext["version"], ext["declared_blobs"], ext["leftover"]))
            w("Lua sources are written to `%s_lua/`, mod streams to `%s_mods/`.\n\n"
              % (base, base))
            w("| blob | packed | unpacked | kind |\n|---|---|---|---|\n")
            for b in ext["blobs"]:
                if "error" in b:
                    w("| %d | %d | -- | FAILED: %s |\n"
                      % (b["key"], b["packed"], b["error"]))
                else:
                    w("| %d | %d | %d | %s |\n"
                      % (b["key"], b["packed"], b["unpacked"],
                         "lua" if is_lua(b["data"]) else "mod stream"))
            w("\n### Strings recovered from the mod streams\n\n")
            w("These carry the custom spell, perk and talent names and their \n"
              "tooltips. Strings are u16-length-prefixed with no terminator; \n"
              "the record framing around them is not fully identified.\n\n")
            for b in ext["blobs"]:
                if "data" not in b or is_lua(b["data"]):
                    continue
                strs = mod_strings(b["data"])
                if not strs:
                    continue
                w("#### blob %d (%d strings)\n\n```\n" % (b["key"], len(strs)))
                for st in strs:
                    w("%s\n" % st)
                w("```\n\n")

        if result.get("lua_config"):
            w("## WoWExt3.bin -- client Lua config\n\n```lua\n%s\n```\n"
              % result["lua_config"])

    print("wrote %s.json and %s.md" % (args.out, args.out))
    if result.get("dll"):
        print("  mod types: %d, lua api: %d"
              % (len(result["dll"]["mod_types"]), len(result["dll"]["lua_api"])))
    if result.get("items"):
        print("  item names: %d" % len(result["items"]))
    if result.get("objloc"):
        print("  tracked object names: %d" % len(result["objloc"]["names"]))
    if result.get("ext"):
        blobs = result["ext"]["blobs"]
        good = [b for b in blobs if "data" in b]
        lua = [b for b in good if is_lua(b["data"])]
        print("  blobs: %d/%d unpacked, %d lua, %d mod streams, %d bytes"
              % (len(good), len(blobs), len(lua), len(good) - len(lua),
                 sum(len(b["data"]) for b in good)))


if __name__ == "__main__":
    main()
