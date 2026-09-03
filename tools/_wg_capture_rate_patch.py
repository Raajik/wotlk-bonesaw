#!/usr/bin/env python3
"""Apply the report #206 WG capture-rate fix to the CRLF core file.

Battlefield capture points (Wintergrasp workshops) never got the
OutdoorPvPCaptureRate multiplier that the data-driven OutdoorPvP capture
points honor, so the 25x capture-speed ship left WG conversion at 1x.
Byte-exact patch: src/server is CRLF, the edit tool mangles that.
"""
from pathlib import Path

TARGET = Path(__file__).resolve().parents[1] / "src/server/game/Battlefield/Battlefield.cpp"

OLD = (
    "    // get the difference of numbers\r\n"
    "    float factDiff = ((float)counts[TEAM_ALLIANCE] - (float)counts[TEAM_HORDE]) * diff / BATTLEFIELD_OBJECTIVE_UPDATE_INTERVAL;\r\n"
    "    if (G3D::fuzzyEq(factDiff, 0.0f))\r\n"
    "        return false;\r\n"
    "\r\n"
    "    TeamId challengerId = TEAM_NEUTRAL;\r\n"
    "    float maxDiff = MaxSpeed * diff;\r\n"
)

NEW = (
    "    // get the difference of numbers\r\n"
    "    // Report #206: the capture-rate config (OutdoorPvPCaptureRate) scaled the\r\n"
    "    // data-driven OutdoorPvP points but never the Battlefield ones, so the\r\n"
    "    // 25x capture speed ship left Wintergrasp workshop conversion at 1x --\r\n"
    "    // the Data16=2 rows only raised the clamp ceiling, which a lone\r\n"
    "    // capturer never reached. Same shape as OutdoorPvP: the rate\r\n"
    "    // multiplies both the raw delta and the per-tick clamp.\r\n"
    "    float const captureRate = sWorld->getFloatConfig(CONFIG_OUTDOOR_PVP_CAPTURE_RATE);\r\n"
    "    float factDiff = ((float)counts[TEAM_ALLIANCE] - (float)counts[TEAM_HORDE]) * diff\r\n"
    "        / BATTLEFIELD_OBJECTIVE_UPDATE_INTERVAL * captureRate;\r\n"
    "    if (G3D::fuzzyEq(factDiff, 0.0f))\r\n"
    "        return false;\r\n"
    "\r\n"
    "    TeamId challengerId = TEAM_NEUTRAL;\r\n"
    "    float maxDiff = MaxSpeed * diff * captureRate;\r\n"
)

text = TARGET.read_bytes().decode("utf-8")
n = text.count(OLD)
if n != 1:
    raise SystemExit(f"anchor match count is {n}, expected 1 -- aborting")
TARGET.write_bytes(text.replace(OLD, NEW, 1).encode("utf-8"))
print("patched:", TARGET)
