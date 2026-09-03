from __future__ import annotations

import importlib.util
import subprocess
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).with_name("github_sync.py")
spec = importlib.util.spec_from_file_location("github_sync", MODULE_PATH)
github_sync = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(github_sync)


def sample_report(**overrides):
    report = {
        "id": "102",
        "name": "Muckfuppet",
        "level": "80",
        "at": "1787760000",
        "zone": "Stormwind City",
        "map": "0",
        "zone_id": "1519",
        "x": "-8833.2",
        "y": "628.4",
        "z": "94.1",
        "target_entry": "12345",
        "target_name": "Training Dummy",
        "description": "|cff71d5ff|Hspell:10|h[Blizzard]|h|r does no damage",
        "report_type": "bug",
        "status": "open",
    }
    report.update(overrides)
    return report


class FakeGitHub:
    def __init__(self, existing=None):
        self.existing = existing
        self.created = []

    def find_report_issue(self, report_id, report_type="bug"):
        return self.existing

    def create_issue(self, title, body, labels):
        self.created.append((title, body, labels))
        return {"number": 203, "url": "https://github.test/issues/203"}


class FakeStore:
    def __init__(self):
        self.links = []
        self.failures = []

    def store_github_link(self, report_id, number, url):
        self.links.append((report_id, number, url))

    def store_github_failure(self, report_id, error):
        self.failures.append((report_id, error))


class GitHubSyncTests(unittest.TestCase):
    def test_gh_commands_have_a_bounded_timeout(self):
        calls = []

        def runner(args, **kwargs):
            calls.append(kwargs)
            return subprocess.CompletedProcess(args, 0, "[]", "")

        github_sync.GitHubCLI(runner=runner)._run(["issue", "list"])
        self.assertEqual(calls[0]["timeout"], github_sync.GH_TIMEOUT_SECONDS)

    def test_missing_gh_is_reported_as_runtime_error(self):
        def runner(args, **kwargs):
            raise FileNotFoundError("gh missing")

        with self.assertRaisesRegex(RuntimeError, "could not run gh"):
            github_sync.GitHubCLI(runner=runner)._run(["issue", "list"])

    def test_clean_wow_text_keeps_link_label_and_removes_color_codes(self):
        self.assertEqual(
            github_sync.clean_wow_text("|cff71d5ff|Hspell:10|h[Blizzard]|h|r does no damage"),
            "[Blizzard] does no damage",
        )

    def test_build_issue_has_stable_title_context_and_hidden_marker(self):
        title, body = github_sync.build_issue(sample_report())
        self.assertEqual(title, "[Report #102] [Blizzard] does no damage")
        self.assertIn("Reported by: `Muckfuppet` (level 80)", body)
        self.assertIn("Stormwind City", body)
        self.assertIn("Training Dummy (entry 12345)", body)
        self.assertIn("<!-- bonesaw-report-id:102 -->", body)
        self.assertNotIn("account", body.lower())

    def test_build_issue_omits_empty_target(self):
        _, body = github_sync.build_issue(sample_report(target_name="", target_entry="0"))
        self.assertNotIn("Target:", body)

    def test_feature_request_gets_feature_title_and_enhancement_label(self):
        report = sample_report(report_type="feature", description="Add account-wide guild mail")
        title, _ = github_sync.build_issue(report)
        self.assertEqual(title, "[Feature #102] Add account-wide guild mail")
        self.assertEqual(
            github_sync.labels_for_report(report),
            ["enhancement", "source:in-game", "status:needs-triage"],
        )

    def test_title_is_bounded_without_losing_report_prefix(self):
        title, _ = github_sync.build_issue(sample_report(description="x" * 500))
        self.assertTrue(title.startswith("[Report #102] "))
        self.assertLessEqual(len(title), github_sync.MAX_TITLE_LENGTH)

    def test_full_addon_length_title_is_not_truncated(self):
        # The addon sends at most 230 chars; with the prefix that must fit
        # under the 256-char GitHub title cap without losing the tail.
        description = "y" * 230
        title, _ = github_sync.build_issue(sample_report(description=description))
        self.assertEqual(title, "[Report #102] " + description)
        self.assertFalse(title.endswith("..."))

    def test_sync_reuses_existing_issue_instead_of_creating_duplicate(self):
        github = FakeGitHub(existing={"number": 102, "url": "https://github.test/issues/102"})
        store = FakeStore()
        result = github_sync.sync_report(sample_report(), github, store)
        self.assertEqual(result["number"], 102)
        self.assertEqual(github.created, [])
        self.assertEqual(store.links, [(102, 102, "https://github.test/issues/102")])

    def test_sync_creates_issue_with_triage_labels_and_stores_link(self):
        github = FakeGitHub()
        store = FakeStore()
        result = github_sync.sync_report(sample_report(), github, store)
        self.assertEqual(result["number"], 203)
        self.assertEqual(
            github.created[0][2],
            ["bug", "source:in-game", "status:needs-triage"],
        )
        self.assertEqual(store.links, [(102, 203, "https://github.test/issues/203")])

    def test_sync_records_failure_without_marking_report_linked(self):
        class BrokenGitHub(FakeGitHub):
            def create_issue(self, title, body, labels):
                raise RuntimeError("rate limited")

        store = FakeStore()
        with self.assertRaisesRegex(RuntimeError, "rate limited"):
            github_sync.sync_report(sample_report(), BrokenGitHub(), store)
        self.assertEqual(store.links, [])
        self.assertEqual(store.failures, [(102, "rate limited")])

    def test_fixed_resolution_closes_with_explicit_fixed_label(self):
        actions = github_sync.resolution_actions("fixed", "Shipped in 0.1.92")
        self.assertEqual(actions[0], ("comment", "Shipped in 0.1.92"))
        self.assertIn(("add-label", "resolution:fixed"), actions)
        self.assertIn(("close", "completed"), actions)

    def test_attempted_resolution_stays_open_for_retest(self):
        actions = github_sync.resolution_actions("attempted", "Added diagnostics")
        self.assertIn(("reopen", ""), actions)
        self.assertIn(("add-label", "status:awaiting-retest"), actions)
        self.assertNotIn(("close", "completed"), actions)

    def test_reopen_removes_resolution_labels(self):
        actions = github_sync.resolution_actions("open", "")
        self.assertIn(("reopen", ""), actions)
        self.assertIn(("remove-label", "resolution:fixed"), actions)
        self.assertIn(("add-label", "status:needs-triage"), actions)

    def test_only_missing_workflow_labels_are_created(self):
        missing = github_sync.labels_to_create({"source:in-game", "status:verified"})
        self.assertNotIn("source:in-game", missing)
        self.assertNotIn("status:verified", missing)
        self.assertIn("status:needs-triage", missing)


if __name__ == "__main__":
    unittest.main()
