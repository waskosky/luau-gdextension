#!/usr/bin/env python3
"""Clone or verify the exact native dependency revisions in deps.lock.json."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT / "deps.lock.json").read_text(encoding="utf-8"))


def run(*arguments: str, cwd: Path | None = None) -> str:
    completed = subprocess.run(arguments, cwd=cwd, check=True, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return completed.stdout.strip()


def ensure(name: str, *, clean: bool) -> dict[str, str]:
    descriptor = LOCK["dependencies"][name]
    destination = ROOT / "deps" / name
    if clean and destination.exists():
        shutil.rmtree(destination)
    created = not destination.exists()
    if created:
        run("git", "clone", "--filter=blob:none", "--no-checkout", descriptor["repository"], str(destination))
    if not (destination / ".git").exists():
        raise RuntimeError(f"{destination} exists but is not a Git checkout")
    if not created:
        status = run("git", "status", "--porcelain", cwd=destination)
        # A prior interrupted no-checkout clone has an empty worktree and an
        # all-deleted index. It is safe to finish materializing that exact
        # locked revision, but never overwrite edits in a populated checkout.
        tracked_files = run("git", "ls-files", cwd=destination).splitlines()
        populated = any((destination / path).exists() for path in tracked_files)
        if status and populated:
            raise RuntimeError(f"dependency checkout is dirty: {name}")
    run("git", "fetch", "--depth", "1", "origin", descriptor["commit"], cwd=destination)
    run("git", "checkout", "--detach", "--force", descriptor["commit"], cwd=destination)
    status = run("git", "status", "--porcelain", cwd=destination)
    if status:
        raise RuntimeError(f"dependency checkout did not materialize cleanly: {name}")
    actual = run("git", "rev-parse", "HEAD", cwd=destination)
    if actual != descriptor["commit"]:
        raise RuntimeError(f"dependency revision mismatch for {name}: {actual}")
    return {"name": name, "commit": actual, "repository": descriptor["repository"]}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clean", action="store_true", help="Delete dependency checkouts before cloning")
    args = parser.parse_args()
    try:
        result = [ensure(name, clean=args.clean) for name in sorted(LOCK["dependencies"])]
    except (RuntimeError, subprocess.CalledProcessError) as exc:
        print(json.dumps({"ok": False, "error": str(exc)}, sort_keys=True))
        return 1
    print(json.dumps({"ok": True, "dependencies": result}, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
