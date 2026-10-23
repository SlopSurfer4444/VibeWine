#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Host CPU tests. No Wine, PE execution, runtime changes, or GPU use."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

import analyze
import build


def records(mode="none", msaa="pass"):
    result = [{"schema": 1, "type": "start", "probe": "gpu-wave", "negative_control": mode}]
    result += [{"schema": 1, "type": "module", "name": name, "path": "C:\\quarantine\\" + name}
               for name in sorted(analyze.MODULES)]
    for name in analyze.STAGES:
        status = msaa if name == "msaa" else "pass"
        if analyze.NEGATIVE_STAGE.get(mode) == name:
            status = "fail"
        result.append({"schema": 1, "type": "stage", "name": name, "status": status,
                       "checks": 0 if status == "unsupported" else 9, "detail": "fixed golden"})
        if status == "fail":
            break
    return finalize(result, "negative_control_rejected" if mode != "none" else "pass")


def finalize(result, status):
    result = copy.deepcopy(result)
    if result[-1].get("type") == "summary":
        result.pop()
    stages = [row for row in result if row["type"] == "stage"]
    result.append({"schema": 1, "type": "summary", "status": status,
                   "stages_passed": sum(row["status"] == "pass" for row in stages),
                   "stages_unsupported": sum(row["status"] == "unsupported" for row in stages),
                   "checks": sum(row["checks"] for row in stages) + 12, "non_stage_checks": 12,
                   "negative_controls_rejected": int(status == "negative_control_rejected")})
    return result


def encode(result):
    return ("\n".join(json.dumps(row) for row in result) + "\n").encode()


class ReportTests(unittest.TestCase):
    def invalid(self, result, code=0, mode="none"):
        with self.assertRaises(analyze.InvalidReport):
            analyze.validate(encode(result), code, mode)

    def test_normal_and_explicit_msaa_unsupported(self):
        for msaa in ("pass", "unsupported"):
            with self.subTest(msaa=msaa):
                report = analyze.validate(encode(records(msaa=msaa)), 0)
                self.assertEqual(report["status"], "pass")
                self.assertEqual(report["stages_passed"], 5 if msaa == "pass" else 4)
                self.assertEqual(report["stages_unsupported"], int(msaa == "unsupported"))

    def test_negative_controls_reach_only_intended_failure(self):
        for mode in analyze.NEGATIVE_STAGE:
            with self.subTest(mode=mode):
                report = analyze.validate(encode(records(mode)), 20, mode)
                self.assertEqual(report["status"], "negative_control_rejected")
                self.invalid(records(mode), 0, mode)
                self.invalid(records(mode), 20, "none")

    def test_early_failure_is_failure_even_before_modules(self):
        result = finalize(records()[:1], "fail")
        self.assertEqual(analyze.validate(encode(result), 10)["status"], "fail")
        self.invalid(result, 0)

    def test_mandatory_unsupported_is_not_pass(self):
        result = records()[:6]
        result[-1]["status"] = "unsupported"
        result[-1]["checks"] = 0
        result = finalize(result, "unsupported")
        self.assertEqual(analyze.validate(encode(result), 2)["status"], "unsupported")
        self.invalid(finalize(result, "pass"), 0)
        self.invalid(result, 0)

    def test_missing_stage_or_duplicated_stage_cannot_pass(self):
        for index in range(5, 10):
            result = records()
            del result[index]
            self.invalid(finalize(result, "pass"))
        result = records()
        result[6] = copy.deepcopy(result[5])
        self.invalid(result)

    def test_wrong_order_summary_and_checks(self):
        result = records()
        result[5], result[6] = result[6], result[5]
        self.invalid(result)
        for field in ("checks", "non_stage_checks", "stages_passed", "stages_unsupported", "negative_controls_rejected"):
            result = records()
            result[-1][field] += 1
            self.invalid(result)
        result = records()
        result[5]["checks"] = 0
        self.invalid(finalize(result, "pass"))

    def test_missing_wrong_empty_or_late_module(self):
        result = records()
        del result[1]
        self.invalid(result)
        for value in ("unknown.dll", [], None):
            result = records()
            result[1]["name"] = value
            self.invalid(result)
        for value in ("", "\ud800", "bad\u0000path", "x" * 4097):
            result = records()
            result[1]["path"] = value
            self.invalid(result)
        result = records()
        result[4], result[5] = result[5], result[4]
        self.invalid(result)

    def test_no_success_from_stderr_text_or_missing_summary(self):
        data = encode(records())
        for malformed in (b"PASS\n", b"wine: error\n" + data, data + b"stderr pass\n",
                          b"\n" + data, encode(records()[:-1]), b"", b"{}\n"):
            with self.subTest(length=len(malformed)):
                with self.assertRaises(analyze.InvalidReport):
                    analyze.validate(malformed, 0)
        for code in (2, 10, 20, 124, -9):
            self.invalid(records(), code)

    def test_bounded_strict_json(self):
        data = encode(records())
        bad = [b"x" * (analyze.MAX_BYTES + 1), b"x" * (analyze.MAX_LINE_BYTES + 1) + b"\n{}\n",
               data + b"{}\n", b"\xff\n", data.replace(b'"schema": 1', b'"schema": true', 1),
               data.replace(b'"schema": 1', b'"schema": 1, "schema": 1', 1),
               data.replace(b'"checks": 9', b'"checks": NaN', 1),
               data.replace(b'"checks": 9', b'"checks": -1', 1),
               data.replace(b'"checks": 9', b'"checks": true', 1),
               data.replace(b'"checks": 9', b'"checks": 1.5', 1),
               b"[" * 2000 + b"]" * 2000 + b"\n{}\n"]
        for candidate in bad:
            with self.subTest(length=len(candidate)):
                with self.assertRaises(analyze.InvalidReport):
                    analyze.validate(candidate, 0)

    def test_negative_rejection_needs_target_and_positive_checks(self):
        result = records("query")
        result[5]["status"] = "fail"
        self.invalid(finalize(result, "negative_control_rejected"), 20, "query")
        result = records("pixel")
        result[5]["checks"] = 0
        self.invalid(finalize(result, "negative_control_rejected"), 20, "pixel")
        result = records("pixel")
        result[-1]["negative_controls_rejected"] = 0
        self.invalid(result, 20, "pixel")

    def test_failed_stage_not_promoted_and_unknown_schema_rejected(self):
        result = records()
        result[7]["status"] = "fail"
        self.invalid(finalize(result, "pass"))
        result = records()
        result[5]["extra"] = "not in fixed schema"
        self.invalid(result)
        result = records()
        result[5]["hr"] = "0x80004005"
        self.assertEqual(analyze.validate(encode(result), 0)["status"], "pass")
        result[5]["hr"] = "0x0"
        self.invalid(result)

    def test_summary_blocker_details_and_non_stage_counters(self):
        result = finalize(records()[:1], "fail")
        result[-1].update(detail="create_device", hr="0x80004005")
        self.assertEqual(analyze.validate(encode(result), 10)["non_stage_checks"], 12)
        result[-1]["hr"] = "0x0"
        self.invalid(result, 10)
        result = records()
        del result[-1]["non_stage_checks"]
        self.invalid(result)

    def test_device_unsupported_before_stages_and_device_removed_after_stages(self):
        result = finalize(records()[:1], "unsupported")
        self.invalid(result, 2)
        result[-1].update(detail="feature_level_11_hardware_unsupported", hr="0x887a0004")
        self.assertEqual(analyze.validate(encode(result), 2)["status"], "unsupported")
        result = finalize(records(), "fail")
        result[-1].update(detail="device_not_removed", hr="0x887a0005")
        self.assertEqual(analyze.validate(encode(result), 10)["status"], "fail")


class BuildHelpersTests(unittest.TestCase):
    def test_imports_are_parsed_and_normalized(self):
        self.assertEqual(build.imports_from("  DLL Name: D3D11.dll\n DLL Name: dxgi.dll\n"), ["d3d11.dll", "dxgi.dll"])
        with self.assertRaises(ValueError):
            build.imports_from("not a PE import table")

    def test_tool_requires_absolute_executable(self):
        with self.assertRaises(ValueError):
            build.tool_path("clang++", "compiler")
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "not-executable"
            path.write_text("fixture")
            with self.assertRaises(ValueError):
                build.tool_path(str(path), "compiler")

    def test_tool_keeps_cross_compiler_symlink_argv_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / "wrapper"
            target.write_text("fixture")
            target.chmod(0o700)
            alias = Path(temporary) / "x86_64-w64-mingw32-clang++"
            alias.symlink_to(target.name)
            self.assertEqual(build.tool_path(str(alias), "compiler"), alias)

    def test_tool_output_is_drained_but_bounded(self):
        result = build.bounded_command([sys.executable, "-c", "print('x' * 20000)"], 5, cap=123)
        self.assertEqual(result["returncode"], 0)
        self.assertEqual(len(result["output"]), 123)
        self.assertEqual(result["outputBytes"], 20001)
        self.assertTrue(result["outputTruncated"])

    def test_timeout_is_failure_and_does_not_run_guest(self):
        result = build.bounded_command([sys.executable, "-c", "import time; time.sleep(3)"], 1)
        self.assertTrue(result["timedOut"])
        self.assertNotEqual(result["returncode"], 0)


if __name__ == "__main__":
    unittest.main()
