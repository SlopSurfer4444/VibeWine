#!/usr/bin/env python3
"""Exercise exact shipping ntdll hooks through a native unit seam, not Wine integration.

Only external dependencies are mocked. The function bodies are extracted without
rewriting; each receipt binds the source bytes used. No network calls or install.
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

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
FUNCTIONS = ["netdiag_endpoint", "netdiag_iov_size", "netdiag_result", "netdiag_attempt",
             "netdiag_recvmsg", "try_recv", "try_send", "async_recv_proc", "async_send_proc",
             "is_icmp_over_dgram", "sock_ioctl_recv", "sock_ioctl_send"]


def balanced(text, opening):
    """C brace scanner honoring strings, character literals, and both comments."""
    depth, pos, state = 0, opening, "code"
    while pos < len(text):
        char, pair = text[pos], text[pos:pos + 2]
        if state == "line":
            if char == "\n": state = "code"
        elif state == "comment":
            if pair == "*/": state, pos = "code", pos + 1
        elif state in ('"', "'"):
            if char == "\\": pos += 1
            elif char == state: state = "code"
        elif pair == "//": state, pos = "line", pos + 1
        elif pair == "/*": state, pos = "comment", pos + 1
        elif char in ('"', "'"): state = char
        elif char == "{": depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0: return pos + 1
        pos += 1
    raise AssertionError("unterminated C body")


def extract(text, name, structure=False):
    pattern = (r"^struct " + re.escape(name) + r"\s*\n\{") if structure else (
        r"^(?:static )?[\w *]+\b" + re.escape(name) + r"\s*\([^;{]*\)\s*\{")
    matches = list(re.finditer(pattern, text, re.M))
    assert len(matches) == 1, (name, len(matches))
    start = matches[0].start()
    end = balanced(text, text.index("{", start))
    if structure:
        assert text[end] == ";"
        end += 1
    return text[start:end]


def main():
    socket = ROOT / "dlls/ntdll/unix/socket.c"
    virtual = ROOT / "dlls/ntdll/unix/virtual.c"
    texts = {socket: socket.read_text(), virtual: virtual.read_text()}
    chunks, receipts = [], []
    for path, name, structure in (
        [(socket, name, True) for name in ("async_recv_ioctl", "async_send_ioctl")] +
        [(socket, name, False) for name in FUNCTIONS] +
        [(virtual, "virtual_locked_recvmsg", False)]
    ):
        chunk = extract(texts[path], name, structure)
        line = texts[path].count("\n", 0, texts[path].index(chunk)) + 1
        chunks.append(f'#line {line} "{path}"\n{chunk}\n')
        receipts.append({"source": str(path.relative_to(ROOT)), "symbol": name,
                         "sha256": hashlib.sha256(chunk.encode()).hexdigest()})
    for path in (ROOT / "include/wine/netdiag_unix.h", ROOT / "include/wine/netdiag.h",
                 HERE / "native-hook-fixture.c"):
        receipts.append({"source": str(path.relative_to(ROOT)),
                         "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    with tempfile.TemporaryDirectory(prefix="wine-native-hooks-") as temp:
        directory = Path(temp)
        (directory / "shipping-hooks.inc").write_text("\n".join(chunks))
        compiler = shlex.split(os.environ.get("CC", "cc"))
        assert compiler and shutil.which(compiler[0]), "C compiler unavailable"
        command = compiler + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                              "-I", str(ROOT / "include"), "-I", str(directory),
                              str(HERE / "native-hook-fixture.c"), "-o", str(directory / "native-hooks")]
        subprocess.run(command, check=True, timeout=60)
        checks, failures = [], []
        for case in ("unarmed", "errno", "writewatch", "async", "send_errno",
                     "allocation_failure", "recv_fd_error", "send_fd_error"):
            run = subprocess.run([str(directory / "native-hooks"), case], text=True,
                                 capture_output=True, timeout=30)
            if run.returncode:
                failures.append({"case": case, "returncode": run.returncode,
                                 "stdout": run.stdout, "stderr": run.stderr})
            else:
                checks.extend(run.stdout.splitlines())
        print(json.dumps({"scope": "exact extracted shipping C functions with mocked external calls; not Wine integration",
                          "compiler_flags": ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror"],
                          "shipping_only_warning_exceptions": ["-Wsign-compare", "-Wunused-parameter"],
                          "checks": checks, "failures": failures, "sources": receipts}, indent=2))
        if failures:
            raise SystemExit(1)


if __name__ == "__main__":
    main()
