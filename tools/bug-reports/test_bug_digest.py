from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


MODULE_PATH = Path(__file__).with_name("bug_digest.py")
sys.path.insert(0, str(MODULE_PATH.parent))
spec = importlib.util.spec_from_file_location("bug_digest", MODULE_PATH)
bug_digest = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(bug_digest)


class BugDigestTests(unittest.TestCase):
    def test_github_sync_lock_allows_only_one_writer(self):
        with tempfile.TemporaryDirectory() as directory:
            lock_path = Path(directory) / "sync.lock"
            with bug_digest.github_sync_lock(lock_path) as first:
                with bug_digest.github_sync_lock(lock_path) as second:
                    self.assertTrue(first)
                    self.assertFalse(second)

    def test_github_sync_lock_failure_does_not_block_delivery(self):
        with patch.object(bug_digest.os, "open", side_effect=PermissionError("denied")):
            with bug_digest.github_sync_lock(Path("unavailable.lock")) as acquired:
                self.assertFalse(acquired)

    def test_dry_run_and_test_mode_never_sync_github(self):
        self.assertFalse(bug_digest.should_sync_github(dry_run=True, test_mode=False))
        self.assertFalse(bug_digest.should_sync_github(dry_run=False, test_mode=True))
        self.assertTrue(bug_digest.should_sync_github(dry_run=False, test_mode=False))

    def test_fetch_candidates_includes_open_report_without_github_link(self):
        fields = [
            "102", "Muckfuppet", "80", "1787760000", "Stormwind", "0", "1519",
            "1.0", "2.0", "3.0", "0", "", "Something broke", "bug", "open", "0", "",
        ]
        raw = bug_digest.FIELD_SEP.join(fields) + bug_digest.ROW_SEP
        with patch.object(bug_digest, "run_sql", return_value=raw):
            rows = bug_digest.fetch_sync_candidates("pw")
        self.assertEqual(rows[0]["id"], "102")
        self.assertEqual(rows[0]["status"], "open")
        self.assertEqual(rows[0]["github_issue_number"], "0")

    def test_discord_report_includes_github_issue_link(self):
        row = {
            "id": "102", "name": "Muckfuppet", "level": "80", "at": "1787760000",
            "zone": "Stormwind", "map": "0", "zone_id": "1519", "x": "1", "y": "2", "z": "3",
            "target_entry": "0", "target_name": "", "description": "Something broke",
            "report_type": "bug",
            "github_issue_url": "https://github.com/Raajik/wotlk-bonesaw/issues/102",
        }
        text = bug_digest.format_report(row)
        self.assertIn("GitHub: https://github.com/Raajik/wotlk-bonesaw/issues/102", text)

    def test_discord_feature_request_is_identified_as_feature(self):
        row = {
            "id": "103", "name": "Ny", "level": "80", "at": "1787760000",
            "zone": "Dalaran", "map": "571", "zone_id": "4395", "x": "1", "y": "2", "z": "3",
            "target_entry": "0", "target_name": "", "description": "Add guild mail",
            "report_type": "feature", "github_issue_url": "https://github.test/issues/103",
        }
        self.assertTrue(bug_digest.format_report(row).startswith("**Feature #103**"))


if __name__ == "__main__":
    unittest.main()
