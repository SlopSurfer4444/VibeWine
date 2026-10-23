#!/usr/bin/env python3
"""Run exact shipping readiness bodies against deterministic native seams.

Reuses the brace-aware extractor and source receipts from run_native_hooks.py.
This performs no Wine, network, installation, UI, game, or process probes.
LGPL-2.1-or-later.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile

from run_native_hooks import extract as brace_extract

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]


def extract(text, name, structure=False):
    if structure:
        return brace_extract(text, name, True)
    # The original extractor can accept a documentation line " * select (...)"
    # spanning into the declaration. Anchor its input at the genuine declaration.
    declaration = re.search(r"^\w[\w *]*\b" + re.escape(name) + r"\s*\([^;{]*\)\s*\{", text, re.M)
    assert declaration, name
    return brace_extract(text[declaration.start():], name)


def main():
    socket = ROOT / "dlls/ws2_32/socket.c"
    helper = ROOT / "dlls/ws2_32/netdiag_pe.h"
    inputs = (socket, helper, ROOT / "include/wine/netdiag.h", ROOT / "include/wine/afd.h",
              ROOT / "include/ntstatus.h", HERE / "native-readiness-fixture.c",
              HERE / "run_native_readiness.py", HERE / "run_native_hooks.py")
    snapshots = {path: path.read_bytes() for path in inputs}
    texts = {path: snapshots[path].decode() for path in (socket, helper)}
    names = [(socket, name) for name in ("add_fd_to_set", "select", "WSAPoll")]
    # Only reachable shipping helpers are extracted, in original declaration order.
    needed = set(re.findall(r"\b(netdiag_\w+)\s*\(", "\n".join(extract(texts[socket], n) for _, n in names)))
    selected = {}
    while needed - selected.keys():
        name = sorted(needed - selected.keys())[0]
        path = helper if re.search(r"\b" + name + r"\s*\(", texts[helper]) else socket
        chunk = extract(texts[path], name)
        assert chunk in texts[path], name
        selected[name] = (path, chunk)
        needed.update(re.findall(r"\b(netdiag_\w+)\s*\(", chunk))
    ordered = sorted(selected.values(), key=lambda item: (item[0] != helper, texts[item[0]].index(item[1])))
    ordered.extend((path, extract(texts[path], name)) for path, name in names)
    macro = re.search(r"^#define NETDIAG_WAIT_MAX .*", texts[helper], re.M)
    assert macro, "readiness helper cap missing"
    ordered[0:0] = [(helper, macro.group()), (helper, extract(texts[helper], "netdiag_wait", True))]
    chunks, receipts = [], []
    for path, chunk in ordered:
        line = texts[path].count("\n", 0, texts[path].index(chunk)) + 1
        chunks.append(f'#line {line} "{path}"\n{chunk}\n')
        receipts.append({"source": str(path.relative_to(ROOT)), "sha256": hashlib.sha256(chunk.encode()).hexdigest(), "line": line})
    for path in inputs:
        receipts.append({"source": str(path.relative_to(ROOT)), "sha256": hashlib.sha256(snapshots[path]).hexdigest()})
    with tempfile.TemporaryDirectory(prefix="wine-native-readiness-") as temp:
        directory = Path(temp)
        (directory / "shipping-readiness.inc").write_text("\n".join(chunks))
        compiler = shlex.split(os.environ.get("CC", "cc"))
        assert compiler and shutil.which(compiler[0]), "C compiler unavailable"
        flags = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror"]
        subprocess.run(compiler + flags + ["-I", str(ROOT / "include"), "-I", str(directory), str(HERE / "native-readiness-fixture.c"), "-o", str(directory / "readiness")], check=True, timeout=60)
        checks, failures = [], []
        for case in ("validation", "allocation", "alias", "bounded", "compact", "poll", "observer", "status", "unarmed"):
            run = subprocess.run([str(directory / "readiness"), case], text=True, capture_output=True, timeout=10)
            if run.returncode:
                failures.append({"case": case, "returncode": run.returncode, "stdout": run.stdout, "stderr": run.stderr})
            else:
                checks.extend(run.stdout.splitlines())
        for path in inputs:
            assert path.read_bytes() == snapshots[path], f"input changed during check: {path}"
        print(json.dumps({"scope": "exact shipping select/WSAPoll and reachable netdiag helpers; mocked kernel, allocation and observation; not Wine integration", "limitations": ["Windows dependency types and constants are a narrow native seam copied from shipping headers", "kernel readiness, waits, socket lookup and lifetime binding are simulated; no PE/Unix bridge, real readiness, concurrency, Wine, or game integration is exercised"], "compiler_flags": flags, "shipping_only_warning_exceptions": ["-Wunused-parameter", "-Wsign-compare"], "checks": checks, "failures": failures, "sources": receipts}, indent=2))
        if failures:
            raise SystemExit(1)


if __name__ == "__main__":
    main()
