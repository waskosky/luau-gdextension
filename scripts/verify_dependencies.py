#!/usr/bin/env python3
from __future__ import annotations

import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT / "deps.lock.json").read_text(encoding="utf-8"))
errors: list[str] = []
records: list[dict[str, str]] = []
for name, descriptor in sorted(LOCK["dependencies"].items()):
    checkout = ROOT / "deps" / name
    if not (checkout / ".git").exists():
        errors.append(f"missing checkout: {name}")
        continue
    actual = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=checkout, text=True).strip()
    dirty = subprocess.check_output(["git", "status", "--porcelain"], cwd=checkout, text=True).strip()
    if actual != descriptor["commit"]:
        errors.append(f"revision mismatch for {name}: {actual}")
    if dirty:
        errors.append(f"dirty dependency checkout: {name}")
    records.append({"name": name, "commit": actual})
print(json.dumps({"ok": not errors, "dependencies": records, "errors": errors}, indent=2, sort_keys=True))
raise SystemExit(0 if not errors else 1)
