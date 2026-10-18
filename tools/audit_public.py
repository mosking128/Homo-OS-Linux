#!/usr/bin/env python3
"""Fail on common secrets and local-only configuration; never print matched values."""
from pathlib import Path
import fnmatch
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
EXCLUDED = {".git", "build", "output", "tmp", "artifacts", "__pycache__"}
SENSITIVE = (".env", ".env.*", "*.key", "*.pem", "*.pcap", "*.pcapng",
             "wpa_supplicant.conf", "id_rsa*", "id_ed25519*", "known_hosts", "authorized_keys")
PATTERNS = {
    "private-key": re.compile(r"-----BEGIN (?:[A-Z]+ )?PRIVATE KEY-----"),
    "github-token": re.compile(r"\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{50,})\b"),
    "aws-access-key": re.compile(r"\bAKIA[A-Z0-9]{16}\b"),
    "personal-windows-home": re.compile(r"[A-Za-z]:[\\/]Users[\\/][^\\/\s]+"),
    "personal-email-example": re.compile(r"[0-9]{5,}@qq\.com", re.I),
    "private-lan-snapshot": re.compile(r"\b192\.168\.[0-9]{1,3}\.[0-9]{1,3}\b"),
}


def audit():
    findings = []
    checked = 0
    for file in sorted(ROOT.rglob("*")):
        rel = file.relative_to(ROOT)
        if any(part in EXCLUDED or part.startswith("build-") for part in rel.parts):
            continue
        if file.is_symlink():
            findings.append((rel.as_posix(), 0, "symlink"))
            continue
        if not file.is_file():
            continue
        if file.name != ".env.example" and any(fnmatch.fnmatch(file.name, pattern) for pattern in SENSITIVE):
            findings.append((rel.as_posix(), 0, "local-config-file"))
        if rel.parts[:2] == ("platform", "lvgl"):
            continue
        try:
            text = file.read_text(encoding="utf-8")
        except (UnicodeError, OSError):
            continue
        checked += 1
        for line_number, line in enumerate(text.splitlines(), 1):
            for kind, pattern in PATTERNS.items():
                if pattern.search(line):
                    findings.append((rel.as_posix(), line_number, kind))
    for path, line, kind in findings:
        print(f"FAIL {path}:{line}: {kind}")
    if findings:
        return 1
    print(f"PASS public-source audit: {checked} text files; no matched values printed")
    return 0


if __name__ == "__main__":
    sys.exit(audit())
