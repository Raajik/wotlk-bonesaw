from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


MODULE_PATH = Path(__file__).with_name("bug_sync.py")
sys.path.insert(0, str(MODULE_PATH.parent))
spec = importlib.util.spec_from_file_location("bug_sync", MODULE_PATH)
bug_sync = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(bug_sync)


class BugSyncTests(unittest.TestCase):
    def test_github_sync_lock_allows_only_one_writer(self):
        with tempfile.TemporaryDirectory() as directory:
            lock_path = Path(directory) / "sync.lock"
            with bug_sync.github_sync_lock(lock_path) as first:
                with bug_sync.github_sync_lock(lock_path) as second:
                    self.assertTrue(first)
                    self.assertFalse(second)

    def test_github_sync_lock_failure_does_not_raise(self):
        with patch.object(bug_sync.os, "open", side_effect=PermissionError("denied")):
            with bug_sync.github_sync_lock(Path("unavailable.lock")) as acquired:
                self.assertFalse(acquired)

    def test_fetch_candidates_includes_open_report_without_github_link(self):
        fields = [
            "102", "Muckfuppet", "80", "1787760000", "Stormwind", "0", "1519",
            "1.0", "2.0", "3.0", "0", "", "Something broke", "bug", "open", "0", "",
        ]
        raw = bug_sync.FIELD_SEP.join(fields) + bug_sync.ROW_SEP
        with patch.object(bug_sync, "run_sql", return_value=raw):
            rows = bug_sync.fetch_sync_candidates("pw")
        self.assertEqual(rows[0]["id"], "102")
        self.assertEqual(rows[0]["status"], "open")
        self.assertEqual(rows[0]["github_issue_number"], "0")

    def test_candidate_query_skips_resolved_and_already_linked_reports(self):
        with patch.object(bug_sync, "run_sql", return_value="") as run_sql:
            bug_sync.fetch_sync_candidates("pw")
        sql = run_sql.call_args[0][0]
        self.assertIn("status IN ('open', 'attempted')", sql)
        self.assertIn("github_issue_number IS NULL", sql)

    def test_describe_marks_critical_and_recurring(self):
        row = {
            "id": "259", "name": "Decipher", "description": "Door dials are gone",
            "report_type": "bug", "is_critical": "1", "is_recurring": "1",
        }
        text = bug_sync.describe(row)
        self.assertIn("Report #259", text)
        self.assertIn("[critical]", text)
        self.assertIn("[recurring]", text)

    def test_describe_identifies_a_feature_request(self):
        row = {
            "id": "103", "name": "Ny", "description": "Add guild mail",
            "report_type": "feature", "is_critical": "0", "is_recurring": "0",
        }
        self.assertTrue(bug_sync.describe(row).startswith("Feature #103"))


if __name__ == "__main__":
    unittest.main()
