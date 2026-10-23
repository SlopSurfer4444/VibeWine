#!/usr/bin/env python3
"""Compile and run the shipping explicit AX Raise action against lifecycle seams."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="wine-ax-raise-") as directory:
    binary = Path(directory) / "raise-test"
    subprocess.run(["clang", "-fobjc-arc", "-fblocks", "-framework", "Foundation",
                    str(root / "raise.m"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
