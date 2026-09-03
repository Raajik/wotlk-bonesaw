from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path
from unittest.mock import patch


MODULE_PATH = Path(__file__).with_name("bug_resolve.py")
sys.path.insert(0, str(MODULE_PATH.parent))
spec = importlib.util.spec_from_file_location("bug_resolve", MODULE_PATH)
bug_resolve = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(bug_resolve)


class FakeGitHub:
    calls = []

    def __init__(self):
        pass

    def ensure_labels(self):
        pass

    def resolve_issue(self, number, status, note):
        self.calls.append((number, status, note))


class BugResolveTests(unittest.TestCase):
    def test_resolve_updates_linked_github_issue(self):
        FakeGitHub.calls = []
        query_row = bug_resolve.SEP.join(["102", "555", "102"])
        with patch.object(bug_resolve, "run_sql", side_effect=[query_row, ""]), \
             patch.object(bug_resolve, "edit_discord", return_value="Discord updated"), \
             patch.object(bug_resolve, "GitHubCLI", FakeGitHub):
            result = bug_resolve.resolve(102, "fixed", "Shipped in 0.1.92", "pw")
        self.assertEqual(result, 0)
        self.assertEqual(FakeGitHub.calls, [(102, "fixed", "Shipped in 0.1.92")])


if __name__ == "__main__":
    unittest.main()
