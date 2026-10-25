#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Compile/inspect only; reuse the gpu-wave bounded builder without running Wine."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
BUILDER = HERE.parents[1] / 'gpu-wave' / 'build.py'


def main():
    # The reviewed sibling builder resolves guest.cpp beside itself. Give it
    # byte-identical temporary source inputs, with its ordinary caps/receipts.
    with tempfile.TemporaryDirectory(prefix='dxmt-pass-guest-build-') as directory:
        scratch = Path(directory)
        builder = BUILDER.read_bytes()
        guest = (HERE / 'guest.cpp').read_bytes()
        (scratch / 'build.py').write_bytes(builder)
        (scratch / 'guest.cpp').write_bytes(guest)
        result = subprocess.run([sys.executable, str(scratch / 'build.py'), *sys.argv[1:]], timeout=150)
        if result.returncode == 0 and '--help' not in sys.argv:
            print('BUILDER_SHA256', hashlib.sha256(builder).hexdigest())
        return result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
