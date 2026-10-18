#!/usr/bin/env python3
"""Verify the imported source and ordered overlay against pinned hashes."""
import hashlib, json
from pathlib import Path
root = Path(__file__).resolve().parent.parent
vendor = json.loads((root / "vibewine/vendor-manifest.json").read_text())
overlay = json.loads((root / "vibewine/source-overlay.json").read_text())
expected = {row["path"]: row["sha256"] for row in vendor["files"]}
for row in overlay["overlaysInApplicationOrder"]:
    expected[row["destination"]] = row["sha256"]
errors = []
for name, digest in expected.items():
    p = root / name
    if not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest() != digest:
        errors.append(name)
if errors:
    raise SystemExit("Source mismatch: " + ", ".join(errors))
print(f"Verified {len(expected)} source files against vendor and ordered overlays")
