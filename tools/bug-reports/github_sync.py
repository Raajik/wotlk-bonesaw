"""Idempotent GitHub synchronization for in-game Bonesaw reports."""
from __future__ import annotations

import json
import os
import re
import subprocess
from datetime import datetime, timezone
from typing import Callable


DEFAULT_REPO = "Raajik/wotlk-bonesaw"
MAX_TITLE_LENGTH = 180
GH_TIMEOUT_SECONDS = 30
LIFECYCLE_LABELS = [
    "status:needs-triage",
    "status:awaiting-retest",
    "status:verified",
    "resolution:fixed",
    "resolution:wontfix",
    "resolution:duplicate",
]
LABEL_DEFINITIONS = {
    "source:in-game": ("5319e7", "Filed from inside the Bonesaw realm"),
    "status:needs-triage": ("fbca04", "Needs reproduction and categorization"),
    "status:awaiting-retest": ("d4c5f9", "Shipped and waiting for an in-game retest"),
    "status:verified": ("0e8a16", "Verified in game on the shipped version"),
    "resolution:fixed": ("0e8a16", "Resolved by a verified fix"),
    "resolution:wontfix": ("ffffff", "Deliberately not being changed"),
    "resolution:duplicate": ("cfd3d7", "Covered by another issue"),
    "priority:critical": ("b60205", "Player flagged as game-breaking (Critical in the report form)"),
    "recurring": ("d93f0b", "Reported still broken / keeps coming back"),
}


def labels_to_create(existing: set[str]) -> dict[str, tuple[str, str]]:
    return {name: value for name, value in LABEL_DEFINITIONS.items() if name not in existing}


def labels_for_report(report: dict) -> list[str]:
    kind = "enhancement" if report.get("report_type") == "feature" else "bug"
    labels = [kind, "source:in-game", "status:needs-triage"]
    # is_critical replaces the old '.crit' report_type encoding; recurring
    # marks still-not-working feedback on a previous fix.
    if str(report.get("is_critical", "0")) in ("1", "true", "True"):
        labels.append("priority:critical")
    if str(report.get("is_recurring", "0")) in ("1", "true", "True"):
        labels.append("recurring")
    return labels


def clean_wow_text(text: str) -> str:
    """Turn WoW color/hyperlink markup into readable plain text."""
    text = re.sub(r"\|H[^|]*\|h(.*?)\|h", r"\1", text or "")
    text = re.sub(r"\|c[0-9a-fA-F]{8}", "", text)
    text = text.replace("|r", "")
    text = re.sub(r"[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]", "", text)
    return " ".join(text.split())


def _reported_at(value: str) -> str:
    try:
        return datetime.fromtimestamp(int(value), tz=timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    except (TypeError, ValueError, OverflowError, OSError):
        return "unknown time"


def build_issue(report: dict) -> tuple[str, str]:
    report_id = int(report["id"])
    description = clean_wow_text(report.get("description", ""))
    is_feature = report.get("report_type") == "feature"
    is_critical = str(report.get("is_critical", "0")) in ("1", "true", "True")
    is_recurring = str(report.get("is_recurring", "0")) in ("1", "true", "True")
    prefix = f"[{'Feature' if is_feature else 'Report'} #{report_id}] "
    title = prefix + description
    if len(title) > MAX_TITLE_LENGTH:
        title = title[: MAX_TITLE_LENGTH - 3].rstrip() + "..."

    where = clean_wow_text(report.get("zone", "")) or f"map {report.get('map', '0')}"
    lines = [
        "## In-game feature request" if is_feature else "## In-game report",
        "",
        description,
        "",
    ]
    flags = []
    if is_critical:
        flags.append("- **Critical** (player-flagged game-breaking)")
    if is_recurring:
        flags.append("- **Recurring** (still not working / keeps coming back)")
    if flags:
        lines.extend(["## Flags", "", *flags, ""])
    lines.extend([
        "## Captured context",
        "",
        f"- Reported by: `{clean_wow_text(report.get('name', 'unknown'))}` (level {report.get('level', '0')})",
        f"- Reported at: {_reported_at(report.get('at', '0'))}",
        f"- Location: `{where}` at `{report.get('x', '0')} {report.get('y', '0')} {report.get('z', '0')}` "
        f"(map {report.get('map', '0')}, zone {report.get('zone_id', '0')})",
    ])
    target_name = clean_wow_text(report.get("target_name", ""))
    target_entry = str(report.get("target_entry", "0"))
    if target_name:
        target = target_name
        if target_entry and target_entry != "0":
            target += f" (entry {target_entry})"
        lines.append(f"- Target: {target}")
    lines.extend([
        "",
        "## Triage",
        "",
        "- [ ] Reproduce or inspect worldserver logs",
        "- [ ] Record the acceptance test",
        "- [ ] Mark shipped version",
        "- [ ] Verify in game before closing",
        "",
        f"<!-- bonesaw-report-id:{report_id} -->",
    ])
    return title, "\n".join(lines)


def resolution_actions(status: str, note: str) -> list[tuple[str, str]]:
    actions: list[tuple[str, str]] = []
    if note:
        actions.append(("comment", note))
    for label in LIFECYCLE_LABELS:
        actions.append(("remove-label", label))
    if status == "open":
        actions.extend([("add-label", "status:needs-triage"), ("reopen", "")])
    elif status == "attempted":
        actions.extend([("add-label", "status:awaiting-retest"), ("reopen", "")])
    elif status == "fixed":
        actions.extend([
            ("add-label", "resolution:fixed"),
            ("close", "completed"),
        ])
    elif status == "wontfix":
        actions.extend([("add-label", "resolution:wontfix"), ("close", "not planned")])
    elif status == "duplicate":
        actions.extend([("add-label", "resolution:duplicate"), ("close", "not planned")])
    else:
        raise ValueError(f"unsupported report status: {status}")
    return actions


class GitHubCLI:
    def __init__(self, repo: str | None = None, runner: Callable = subprocess.run):
        self.repo = repo or os.environ.get("BONESAW_GITHUB_REPO", DEFAULT_REPO)
        self.runner = runner

    def _run(self, args: list[str]) -> str:
        try:
            proc = self.runner(
                ["gh", *args, "--repo", self.repo],
                capture_output=True,
                text=True,
                timeout=GH_TIMEOUT_SECONDS,
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            raise RuntimeError(f"could not run gh: {exc}") from exc
        if proc.returncode != 0:
            raise RuntimeError(proc.stderr.strip() or f"gh exited {proc.returncode}")
        return proc.stdout.strip()

    @staticmethod
    def _json(raw: str, context: str):
        try:
            return json.loads(raw or "[]")
        except (TypeError, json.JSONDecodeError) as exc:
            raise RuntimeError(f"invalid JSON from gh while {context}") from exc

    def ensure_labels(self) -> None:
        raw = self._run(["label", "list", "--limit", "100", "--json", "name"])
        existing = {item["name"] for item in self._json(raw, "listing labels")}
        for name, (color, description) in labels_to_create(existing).items():
            self._run(["label", "create", name, "--color", color,
                       "--description", description])

    def find_report_issue(self, report_id: int, report_type: str = "bug") -> dict | None:
        preferred = "Feature" if report_type == "feature" else "Report"
        for kind in (preferred, "Report" if preferred == "Feature" else "Feature"):
            raw = self._run([
                "issue", "list", "--state", "all", "--limit", "20",
                "--search", f'\"[{kind} #{report_id}]\" in:title',
                "--json", "number,title,url",
            ])
            prefix = f"[{kind} #{report_id}]"
            for issue in self._json(raw, f"searching for {kind.lower()} #{report_id}"):
                if issue.get("title", "").startswith(prefix):
                    return {"number": int(issue["number"]), "url": issue["url"]}
        return None

    def create_issue(self, title: str, body: str, labels: list[str]) -> dict:
        raw = self._run([
            "issue", "create", "--title", title, "--body", body,
            "--label", ",".join(labels),
        ])
        url = raw.splitlines()[-1].strip()
        match = re.search(r"/issues/(\d+)$", url)
        if not match:
            raise RuntimeError(f"could not parse created issue URL: {url}")
        return {"number": int(match.group(1)), "url": url}

    def resolve_issue(self, number: int, status: str, note: str) -> None:
        current = self._json(self._run([
            "issue", "view", str(number), "--json", "state,labels",
        ]), f"reading issue #{number}")
        state = current.get("state", "OPEN").upper()
        labels = {item["name"] for item in current.get("labels", [])}
        for action, value in resolution_actions(status, note):
            if action == "comment":
                self._run(["issue", "comment", str(number), "--body", value])
            elif action == "remove-label" and value in labels:
                self._run(["issue", "edit", str(number), "--remove-label", value])
                labels.discard(value)
            elif action == "add-label" and value not in labels:
                self._run(["issue", "edit", str(number), "--add-label", value])
                labels.add(value)
            elif action == "reopen" and state != "OPEN":
                self._run(["issue", "reopen", str(number)])
                state = "OPEN"
            elif action == "close" and state != "CLOSED":
                self._run(["issue", "close", str(number), "--reason", value])
                state = "CLOSED"


class SqlReportStore:
    def __init__(self, run_sql: Callable[[str, str], str], db_password: str):
        self.run_sql = run_sql
        self.db_password = db_password

    @staticmethod
    def _escape(text: str) -> str:
        return text.replace("\\", "\\\\").replace("'", "''")

    def store_github_link(self, report_id: int, number: int, url: str) -> None:
        self.run_sql(
            "UPDATE lg_bug_report SET github_issue_number = %d, github_issue_url = '%s', "
            "github_synced_at = UNIX_TIMESTAMP(), github_sync_error = '' WHERE id = %d;"
            % (number, self._escape(url), report_id),
            self.db_password,
        )

    def store_github_failure(self, report_id: int, error: str) -> None:
        self.run_sql(
            "UPDATE lg_bug_report SET github_sync_attempts = github_sync_attempts + 1, "
            "github_sync_error = '%s' WHERE id = %d;"
            % (self._escape(error[:500]), report_id),
            self.db_password,
        )


def sync_report(report: dict, github, store) -> dict:
    report_id = int(report["id"])
    try:
        issue = github.find_report_issue(report_id, report.get("report_type", "bug"))
        if issue is None:
            title, body = build_issue(report)
            issue = github.create_issue(title, body, labels_for_report(report))
        store.store_github_link(report_id, int(issue["number"]), issue["url"])
        return issue
    except Exception as exc:
        store.store_github_failure(report_id, str(exc))
        if isinstance(exc, RuntimeError):
            raise
        raise RuntimeError(str(exc)) from exc
