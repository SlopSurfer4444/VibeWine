#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Compile and inspect a quarantined PE. This script never invokes Wine/the PE.

Supply reviewed tools explicitly; no host runtime or toolchain is selected.
The new output directory contains a source snapshot and bounded build evidence.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import threading

LOG_CAP = 64 * 1024
SOURCE_CAP = 512 * 1024
BINARY_CAP = 32 * 1024 * 1024
RECEIPT_CAP = 32 * 1024
REQUIRED_IMPORTS = {"d3d11.dll", "dxgi.dll", "d3dcompiler_47.dll"}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def tool_path(value: str, name: str) -> Path:
    path = Path(value)
    if not path.is_absolute():
        raise ValueError(f"{name} must be an absolute path")
    resolved = path.resolve(strict=True)
    if not resolved.is_file() or not os.access(path, os.X_OK):
        raise ValueError(f"{name} must be an executable file")
    # LLVM-MinGW's cross-target wrappers can select their target from argv[0].
    # Preserve the supplied symlink name when invoking the reviewed executable.
    return Path(os.path.abspath(path))


def bounded_command(argv: list[str], timeout: int, cap: int = LOG_CAP) -> dict:
    """Drain output while retaining at most cap bytes; kill only our process group."""
    retained = bytearray()
    total = 0
    process = subprocess.Popen(argv, stdin=subprocess.DEVNULL,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               start_new_session=True)
    assert process.stdout is not None

    def drain() -> None:
        nonlocal total
        while chunk := process.stdout.read(8192):
            total += len(chunk)
            retained.extend(chunk[:max(0, cap - len(retained))])

    reader = threading.Thread(target=drain, daemon=True)
    reader.start()
    timed_out = False
    try:
        process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()
    reader.join(timeout=2)
    if reader.is_alive():
        # A compiler child may retain the pipe after the driver exits.
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        reader.join(timeout=2)
        if reader.is_alive():
            raise RuntimeError("tool output pipe did not close after process-group stop")
    process.stdout.close()
    return {"returncode": process.returncode, "timedOut": timed_out,
            "output": bytes(retained), "outputBytes": total,
            "outputTruncated": total > cap}


def imports_from(text: str) -> list[str]:
    imports = re.findall(r"^\s*DLL Name:\s*([A-Za-z0-9_.-]+)\s*$", text, re.MULTILINE)
    if not imports:
        raise ValueError("objdump produced no PE import table")
    return sorted({name.lower() for name in imports})


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True, help="absolute reviewed LLVM-MinGW clang++ path")
    parser.add_argument("--objdump", help="absolute llvm-objdump path; default adjacent to compiler")
    parser.add_argument("--output", required=True, help="new absolute quarantine directory; parent must exist")
    parser.add_argument("--wine-build", help="optional source/build identity only; never executed or modified")
    parser.add_argument("--source-id", default="", help="optional bounded revision/source label")
    args = parser.parse_args(argv)
    try:
        compiler = tool_path(args.compiler, "compiler")
        if not re.fullmatch(r"x86_64-w64-mingw32-clang\+\+(?:\.exe)?", Path(args.compiler).name):
            raise ValueError("compiler basename must be x86_64-w64-mingw32-clang++")
        objdump = tool_path(args.objdump or str(Path(args.compiler).parent / "llvm-objdump"), "objdump")
        output = Path(args.output)
        if not output.is_absolute() or output.exists() or output.is_symlink():
            raise ValueError("output must be a new absolute directory")
        output = output.parent.resolve(strict=True) / output.name
        if len(args.source_id) > 256 or any(ord(c) < 32 for c in args.source_id):
            raise ValueError("source-id must be at most 256 printable characters")
        wine_build = None
        if args.wine_build:
            supplied = Path(args.wine_build)
            if not supplied.is_absolute() or not supplied.is_dir():
                raise ValueError("wine-build must identify an existing absolute directory")
            wine_build = str(supplied.resolve(strict=True))
        source = Path(__file__).resolve().with_name("guest.cpp")
        with source.open("rb") as stream:
            snapshot = stream.read(SOURCE_CAP + 1)
        if len(snapshot) > SOURCE_CAP:
            raise ValueError("guest source exceeds source byte cap")
        output.mkdir(mode=0o700)
    except (OSError, ValueError) as error:
        parser.error(str(error))

    staged_source = output / "guest.cpp"
    staged_source.write_bytes(snapshot)
    binary = output / "guest.exe"
    command = [str(compiler), "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror", "-static",
               str(staged_source), "-o", str(binary), "-ld3d11", "-ldxgi", "-ld3dcompiler", "-ldxguid"]
    receipt = {"schema": 1, "status": "failed", "argv": command,
               "source": str(source), "sourceSha256": hashlib.sha256(snapshot).hexdigest(),
               "sourceId": args.source_id, "wineBuildIdentityOnly": wine_build,
               "builderSha256": sha256(Path(__file__).resolve()),
               "compiler": {"path": str(compiler), "resolvedPath": str(compiler.resolve()), "sha256": sha256(compiler)},
               "objdump": {"path": str(objdump), "resolvedPath": str(objdump.resolve()), "sha256": sha256(objdump)},
               "runtimeExecuted": False, "installedRuntimeChanged": False,
               "caps": {"logBytesEach": LOG_CAP, "receiptBytes": RECEIPT_CAP,
                        "sourceBytes": SOURCE_CAP, "binaryBytes": BINARY_CAP}}
    try:
        compiled = bounded_command(command, 120)
        (output / "compile.log").write_bytes(compiled.pop("output"))
        receipt["compile"] = compiled
        if compiled["returncode"] != 0 or compiled["timedOut"]:
            raise ValueError("compiler failed or timed out; inspect compile.log")
        if not binary.is_file() or binary.stat().st_size > BINARY_CAP:
            raise ValueError("missing binary or binary exceeds byte cap")
        receipt["binarySha256"] = sha256(binary)
        receipt["binaryBytes"] = binary.stat().st_size
        inspect_argv = [str(objdump), "-p", str(binary)]
        inspected = bounded_command(inspect_argv, 15)
        raw_imports = inspected.pop("output")
        (output / "imports.txt").write_bytes(raw_imports)
        receipt["inspect"] = {"argv": inspect_argv, **inspected}
        if inspected["returncode"] != 0 or inspected["timedOut"] or inspected["outputTruncated"]:
            raise ValueError("import inspection failed, timed out, or exceeded evidence cap")
        imports = imports_from(raw_imports.decode("utf-8", errors="strict"))
        receipt["imports"] = imports
        if not REQUIRED_IMPORTS.issubset(imports):
            raise ValueError("PE is missing mandatory D3D11/DXGI/compiler imports")
        if any(name.startswith(("libstdc++", "libgcc", "libwinpthread", "libc++", "libunwind")) for name in imports):
            raise ValueError("unexpected dynamic C++ runtime import in static build")
        receipt["status"] = "compiled_imports_inspected_only"
    except (OSError, UnicodeError, ValueError, RuntimeError) as error:
        receipt["error"] = str(error)[:2048]
    encoded = (json.dumps(receipt, indent=2, sort_keys=True) + "\n").encode("utf-8")
    if len(encoded) > RECEIPT_CAP:
        raise RuntimeError("build receipt exceeds cap")
    (output / "build.json").write_bytes(encoded)
    print(encoded.decode("utf-8"), end="")
    return 0 if receipt["status"] == "compiled_imports_inspected_only" else 1


if __name__ == "__main__":
    raise SystemExit(main())
