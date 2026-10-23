#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Run the complete production dependency scanner on an isolated mini-project."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import time


def run(argv, cwd, **kwargs):
    env = {key: value for key, value in os.environ.items()
           if key not in ("MAKEFLAGS", "MFLAGS", "GNUMAKEFLAGS", "MAKEOVERRIDES")}
    return subprocess.run(argv, cwd=cwd, text=True, capture_output=True,
                          env=env, timeout=60, **kwargs)


def prerequisites(text, target):
    result = set()
    for line in text.replace("\\\n", " ").splitlines():
        if not line or line.startswith("\t") or ":" not in line:
            continue
        names, dependencies = line.split(":", 1)
        if target in names.split():
            result.update(dependencies.split())
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", help="read complete tools/makedep.c from this Git revision")
    parser.add_argument("--expect-regression", action="store_true")
    parser.add_argument("--cc", default="clang", help="native host C compiler")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    source = (subprocess.check_output(["git", "show", f"{args.revision}:tools/makedep.c"],
                                     cwd=root, text=True, timeout=30) if args.revision else
              (root / "tools/makedep.c").read_text())
    failures = []

    def check(name, condition):
        if not condition:
            failures.append(name)

    with tempfile.TemporaryDirectory(prefix="vibewine-makedep-inc-") as scratch:
        scratch = Path(scratch)
        (scratch / "config.h").write_text("#define __WINE_CONFIG_H\n")
        (scratch / "makedep.c").write_text(source)
        scanner = scratch / "makedep"
        # Compile the entire real scanner and its normal Wine helper headers.
        # Only the tool's platform configuration is minimal; no parser bodies,
        # generated dependency rules or compiler/WIDL implementations are mocked.
        command = [args.cc, "-std=gnu11", "-O1", "-g", "-D_DARWIN_C_SOURCE",
                   "-D__WINESRC__", "-DWINE_UNIX_LIB", "-I", str(scratch),
                   "-I", str(root / "tools"), "-I", str(root / "include"),
                   str(scratch / "makedep.c"), "-o", str(scanner)]
        compiled = run(command, scratch)
        if compiled.returncode:
            raise SystemExit(compiled.stderr)
        project = scratch / "project"
        (project / "include").mkdir(parents=True)
        (project / "sample").mkdir()
        (project / "Makefile").write_text(
            "srcdir = .\nHOST_ARCH = x86_64\nPE_ARCHS = none\n"
            "LN_S = ln -s\nSUBDIRS = include sample\nCC = clang\n")
        (project / "include/Makefile.in").write_text("SOURCES = generated.idl rpc.h rpcndr.h\n")
        # These are terminal nodes in the miniature dependency graph, not an
        # RPC implementation. The fixture does not compile an RPC consumer.
        for name in ("rpc.h", "rpcndr.h"):
            (project / "include" / name).write_text("/* fixture dependency terminal */\n")
        (project / "include/generated.idl").write_text(
            'cpp_quote("#define GENERATED_VALUE 7")\n')
        (project / "sample/Makefile.in").write_text("SOURCES = main.c\n")
        (project / "sample/main.c").write_text(
            '#include "provider.inc"\nint value(void) { return GENERATED_VALUE + LEAF_VALUE; }\n')
        (project / "sample/provider.inc").write_text('#include "nested.inc"\n')
        (project / "sample/nested.inc").write_text(
            '#include "generated.h"\n#include "leaf.h"\n')
        (project / "sample/leaf.h").write_text("#define LEAF_VALUE 3\n")
        scanned = run([str(scanner)], project)
        if scanned.returncode or scanned.stderr:
            raise SystemExit(f"scanner failed: exit={scanned.returncode}\n{scanned.stderr}")
        text = (project / "Makefile").read_text()
        target = "sample/main.o"
        deps = prerequisites(text, target)
        check("direct-include", "sample/provider.inc" in deps)
        check("nested-include", "sample/nested.inc" in deps)
        check("nested-header", "sample/leaf.h" in deps)
        check("generated-idl-header", "include/generated.h" in deps)
        check("generated-idl-rule", "include/generated.idl" in
              prerequisites(text, "include/generated.h"))
        # Execute make's actual scheduler over the unmodified scanner output.
        # -o excludes only fixture bootstrap/tools, which are outside this test;
        # -n prints the real WIDL generation command without running a tool.
        make = ["make", "-r", "-f", "Makefile", "-o", "Makefile", "-o", "config.status",
                "-o", "configure", "-o", "tools/makedep", "-o", "tools/widl/widl"]
        dry = run(make + ["-n", target], project)
        if dry.returncode or dry.stderr:
            raise SystemExit(f"make dry run failed: exit={dry.returncode}\n{dry.stderr}")
        check("missing-header-generation", "tools/widl/widl -o include/generated.h" in dry.stdout)
        # Pre-existing object/header markers model a completed build. They are
        # never executed or presented as compiled code. Probe actual make -q
        # invalidation for independently changed nested include and leaf header.
        (project / "include/generated.h").write_text("/* existing generated header */\n")
        obj = project / target
        obj.write_bytes(b"scheduler fixture object marker\n")
        stamp = time.time() - 100
        for path in project.rglob("*"):
            if path.is_file():
                os.utime(path, (stamp, stamp))
        os.utime(obj, (stamp + 10, stamp + 10))
        baseline = run(make + ["-q", target], project)
        check("unchanged-object", baseline.returncode == 0 and not baseline.stderr)
        for name, relative in [("nested-include-update", "sample/nested.inc"),
                               ("nested-header-update", "sample/leaf.h"),
                               ("generated-header-update", "include/generated.h")]:
            path = project / relative
            os.utime(path, (stamp + 20, stamp + 20))
            changed = run(make + ["-q", target], project)
            check(name, changed.returncode == 1 and not changed.stderr)
            os.utime(path, (stamp, stamp))
        # Wine's normal `make depend` rescans changed include directives. Run
        # the same scanner again and verify newly introduced edges at both
        # nested include and nested header sites, using unchanged assertions.
        (project / "sample/added.h").write_text("#define ADDED_VALUE 1\n")
        (project / "sample/deep.h").write_text("#define DEEP_VALUE 2\n")
        with (project / "sample/nested.inc").open("a") as stream:
            stream.write('#include "added.h"\n')
        with (project / "sample/leaf.h").open("a") as stream:
            stream.write('#include "deep.h"\n')
        rescanned = run([str(scanner)], project)
        if rescanned.returncode or rescanned.stderr:
            raise SystemExit(f"rescan failed: exit={rescanned.returncode}\n{rescanned.stderr}")
        updated = prerequisites((project / "Makefile").read_text(), target)
        check("nested-include-rescan", "sample/added.h" in updated)
        check("nested-header-rescan", "sample/deep.h" in updated)

    expected = ["nested-include", "nested-header", "generated-idl-header",
                "missing-header-generation", "nested-include-update", "nested-header-update",
                "generated-header-update", "nested-include-rescan",
                "nested-header-rescan"] if args.expect_regression else []
    print(f"production SHA256={hashlib.sha256(source.encode()).hexdigest()} "
          f"source={args.revision or 'working-tree'}")
    for name in failures:
        print(f"FAIL {name}")
    if failures != expected:
        raise SystemExit(f"unexpected failures: {failures!r}; expected {expected!r}")
    print(f"MAKEDEP_INC_{'REGRESSION' if args.expect_regression else 'PASS'} "
          f"checks=12 failures={len(failures)}")


if __name__ == "__main__":
    main()
