#!/usr/bin/env python3
"""Production optimizer driver scaling/state fixture; CPU only, no game/GPU."""
import hashlib
import importlib.util
import json
from pathlib import Path
import platform
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
BASELINE_REF = "e1772a425721b4f694a52c20ef92fff7332bafe4"
spec = importlib.util.spec_from_file_location("pass_fixture", HERE.parent / "pass-optimizations/run.py")
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)


def program(contents):
    generated = shared.generate(contents)
    generated = generated[:generated.rindex("\nint main()")]
    assert "namespace dxmt {" in generated
    generated = generated.replace("namespace dxmt {",
        "static uint64_t relation_calls, candidate_visits;\n"
        "#if defined(DXMT_BOUNDED_ENCODER_OPTIMIZER) && DXMT_BOUNDED_ENCODER_OPTIMIZER\n"
        "static constexpr bool bounded = true;\n#else\nstatic constexpr bool bounded = false;\n#endif\n"
        "namespace dxmt {", 1)
    generated = shared.replace(generated,
        "ArgumentEncodingContext::checkEncoderRelation(EncoderData *former, EncoderData *latter) {",
        "ArgumentEncodingContext::checkEncoderRelation(EncoderData *former, EncoderData *latter) {\n++relation_calls;")
    generated = shared.replace(generated, "        if (encoders[i]->type == EncoderType::Null)",
                              "        ++candidate_visits;\n        if (encoders[i]->type == EncoderType::Null)")
    return generated + "\n" + (HERE / "fixture.cpp.in").read_text()


def main():
    contents = shared.sources(REPO)
    original = program(contents)
    legacy_contents = dict(contents)
    legacy_contents["src/dxmt/dxmt_context.cpp"] = subprocess.check_output(
        ["git", "-C", str(REPO), "show", BASELINE_REF + ":src/dxmt/dxmt_context.cpp"], text=True)
    legacy = program(legacy_contents)
    disable = original.replace("if (i - j > kEncoderOptimizerThreshold)", "if (false)")
    null_bypass = shared.replace(original,
        "        if (i - j > kEncoderOptimizerThreshold)\n          break;",
        "        if (i - j > kEncoderOptimizerThreshold && encoders[i]->type != EncoderType::Null)\n          break;")
    drop_on_cutoff = shared.replace(original,
        "        if (i - j > kEncoderOptimizerThreshold)\n          break;",
        "        if (i - j > kEncoderOptimizerThreshold) { encoders[j]->type = EncoderType::Null; break; }")
    variants = [("historical-unbounded", legacy, [], True),
                ("default", original, [], True), ("explicit-off", original, ["-DDXMT_BOUNDED_ENCODER_OPTIMIZER=0"], True),
                ("bounded", original, ["-DDXMT_BOUNDED_ENCODER_OPTIMIZER=1"], True),
                ("bounded-with-pass-experiments", original,
                 ["-DDXMT_BOUNDED_ENCODER_OPTIMIZER=1", "-DDXMT_DEAD_ATTACHMENT_STORES=1",
                  "-DDXMT_PHYSICAL_RESIDENCY_DEDUP=1"], True),
                ("disabled-bound", disable, ["-DDXMT_BOUNDED_ENCODER_OPTIMIZER=1"], False),
                ("null-bypass", null_bypass, ["-DDXMT_BOUNDED_ENCODER_OPTIMIZER=1"], False),
                ("drop-on-cutoff", drop_on_cutoff, ["-DDXMT_BOUNDED_ENCODER_OPTIMIZER=1"], False)]
    results = []
    with tempfile.TemporaryDirectory(prefix="dxmt-encoder-lookahead-") as directory:
        scratch = Path(directory)
        for name, source, flags, accepted in variants:
            cpp, binary = scratch / (name + ".cpp"), scratch / name
            cpp.write_text(source)
            command = ["xcrun", "clang++", "-std=c++20", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-parameter", "-Wno-unused-variable", "-Wno-pragma-once-outside-header",
                "-fsanitize=address,undefined", "-fno-sanitize-recover=all", *flags,
                *["-I" + str(REPO / p) for p in ("src/winemetal", "src/dxmt", "src/util", "src/airconv", "include")],
                str(cpp), "-o", str(binary)]
            subprocess.run(command, check=True, timeout=90)
            if name == "bounded":
                other_arch = "x86_64" if platform.machine() == "arm64" else "arm64"
                subprocess.run([*command[:-2], "-arch", other_arch, "-c", "-o", str(scratch / "other.o")],
                               check=True, timeout=90)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            output = run.stdout + run.stderr
            assert (run.returncode == 0) == accepted, (name, run.returncode, output)
            if not accepted:
                assert run.returncode == 1 and "behavioral assertion:" in output and "sanitizer" not in output.lower(), output
            results.append({"variant": name, "exit": run.returncode, "output": output.strip()})
    assert results[0]["output"] == results[1]["output"] == results[2]["output"], "default differs from historical unbounded behavior"
    print(json.dumps({"passed": True, "scope": "CPU extracted production optimizer/relation/dependency methods; no GPU/Metal/Wine",
                      "other_architecture_compile": other_arch,
                      "baseline_ref": BASELINE_REF,
                      "sources_sha256": {p: hashlib.sha256(c.encode()).hexdigest() for p, c in contents.items()},
                      "results": results}, indent=2))


if __name__ == "__main__":
    main()
