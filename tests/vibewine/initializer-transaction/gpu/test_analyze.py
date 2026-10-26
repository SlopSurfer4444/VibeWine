#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Bounded host CPU parser controls; no Wine, build, runtime changes or GPU."""
import copy
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

import analyze

STAGES = ("private_buffer_zero", "padded_upload_subresources", "zero_texture_growth",
          "render_target_zero", "depth_stencil_zero", "depth_stencil_upload", "padded_volume_upload")
MODULES = ("d3d11.dll", "dxgi.dll", "winemetal.dll", "d3dcompiler_47.dll")
NEGATIVES = {"upload": ("padded_upload_subresources", "negative_initializer_upload_golden"),
             "depth": ("depth_stencil_upload", "negative_initializer_depth_query")}


def finalize(records, status="pass", detail="complete", hr="0x00000000"):
    result = copy.deepcopy(records)
    if result[-1].get("type") == "summary":
        result.pop()
    stages = [record for record in result if record["type"] == "stage"]
    result.append({"schema": 1, "type": "summary", "status": status,
                   "stages_passed": sum(row["status"] == "pass" for row in stages),
                   "stages_unsupported": sum(row["status"] == "unsupported" for row in stages),
                   "checks": sum(row["checks"] for row in stages) + 37, "non_stage_checks": 37,
                   "negative_controls_rejected": int(status == "negative_control_rejected"),
                   "detail": detail, "hr": hr})
    return result


def records(mode="none"):
    result = [{"schema": 1, "type": "start", "probe": "initializer-transaction-gpu", "negative_control": mode}]
    result += [{"schema": 1, "type": "module", "name": name, "path": "C:\\isolated\\" + name}
               for name in MODULES]
    for index, name in enumerate(STAGES):
        negative = mode in NEGATIVES and name == NEGATIVES[mode][0]
        result.append({"schema": 1, "type": "stage", "name": name, "status": "fail" if negative else "pass",
                       "checks": 9 + index, "detail": NEGATIVES[mode][1] if negative else "verified",
                       "hr": "0x80004005" if negative else "0x00000000"})
        if negative:
            return finalize(result, "negative_control_rejected", NEGATIVES[mode][1], "0x80004005")
    return finalize(result)


def encode(result):
    return ("\n".join(json.dumps(row) for row in result) + "\n").encode()


class AnalyzerTests(unittest.TestCase):
    def invalid(self, result, code=0, mode="none"):
        with self.assertRaises(analyze.InvalidReport):
            analyze.validate(encode(result), code, mode)

    def test_seven_names_and_negative_details_bound_to_guest_source(self):
        guest = (Path(__file__).parent / "guest.cpp").read_text()
        self.assertEqual(tuple(re.findall(r'\bstage\("([a-z_]+)"', guest)), STAGES)
        self.assertEqual(analyze.STAGES, STAGES)
        self.assertEqual(analyze.MODULES, MODULES)
        for mode, (stage, detail) in NEGATIVES.items():
            self.assertEqual(analyze.NEGATIVE_STAGE[mode], stage)
            self.assertEqual(analyze.NEGATIVE_DETAIL[mode], detail)
            self.assertIn('"' + detail + '"', guest)

    def test_normal_exactly_thirteen_records_all_seven_stages(self):
        result = records()
        self.assertEqual(len(result), 13)
        report = analyze.validate(encode(result), 0)
        self.assertEqual(report["status"], "pass")
        self.assertEqual(report["stages_passed"], 7)
        self.assertEqual(report["stages_unsupported"], 0)
        self.assertEqual(report["checks"], sum(range(9, 16)) + 37)
        # Poll counts legitimately vary. Preserve arithmetic rather than freezing
        # a timing-dependent successful guest count.
        result[6]["checks"] += 53
        result[-1]["checks"] += 53
        self.assertEqual(analyze.validate(encode(result), 0)["checks"], report["checks"] + 53)

    def test_both_intentional_negatives_stop_at_exact_target(self):
        for mode, (target, detail) in NEGATIVES.items():
            with self.subTest(mode=mode):
                result = records(mode)
                report = analyze.validate(encode(result), 20, mode)
                self.assertEqual(report["status"], "negative_control_rejected")
                self.assertEqual(report["stages"][-1]["name"], target)
                self.assertEqual(report["stages"][-1]["detail"], detail)
                self.assertEqual(report["negative_controls_rejected"], 1)
                self.assertEqual(len(result), 8 if mode == "upload" else 12)
                for code in (0, 2, 10, 124, -9):
                    self.invalid(result, code, mode)
                self.invalid(result, 20)
                self.invalid(result, 20, "depth" if mode == "upload" else "upload")

    def test_every_missing_duplicate_or_reordered_stage_rejected(self):
        for index in range(5, 12):
            result = records()
            del result[index]
            self.invalid(finalize(result))
            result = records()
            result.insert(index, copy.deepcopy(result[index]))
            self.invalid(finalize(result))
        for index in range(5, 11):
            result = records()
            result[index], result[index + 1] = result[index + 1], result[index]
            self.invalid(result)
        result = records()
        result[7]["name"] = "msaa"
        self.invalid(result)

    def test_unsupported_never_accepted_even_with_coherent_counters(self):
        for index in range(5, 12):
            result = records()
            result[index]["status"] = "unsupported"
            result[index]["checks"] = 0
            self.invalid(finalize(result))
            self.invalid(finalize(result, "unsupported"), 2)
        result = finalize(records()[:1], "unsupported", "device_unsupported", "0x887a0004")
        self.invalid(result, 2)

    def test_each_summary_counter_requires_consistent_bounded_integer(self):
        fields = ("stages_passed", "stages_unsupported", "checks", "non_stage_checks", "negative_controls_rejected")
        for field in fields:
            for value in (-1, True, 1.5, 1_000_001, None, "7"):
                result = records()
                result[-1][field] = value
                self.invalid(result)
            result = records()
            result[-1][field] += 1
            self.invalid(result)
        for value in (0, -1, True, 1.5, 1_000_001):
            result = records()
            result[5]["checks"] = value
            # Avoid bool/float normalization by passing the counter unchanged.
            self.invalid(result)

    def test_negative_details_and_hresult_cannot_mask_other_failure(self):
        for mode in NEGATIVES:
            for index in (-1, -2):
                for field, value in (("detail", "texture_create"), ("detail", "negative_control_query"),
                                     ("detail", ""), ("hr", "0x00000000"), ("hr", "0x8007000e")):
                    result = records(mode)
                    result[index][field] = value
                    self.invalid(result, 20, mode)
            result = records(mode)
            result[-2]["checks"] = 0
            result[-1]["checks"] = sum(row["checks"] for row in result if row["type"] == "stage") + 37
            self.invalid(result, 20, mode)
            for count in (0, 2):
                result = records(mode)
                result[-1]["negative_controls_rejected"] = count
                self.invalid(result, 20, mode)

    def test_negative_needs_complete_prefix_and_failed_last_stage(self):
        result = records("depth")
        del result[7]
        self.invalid(finalize(result, "negative_control_rejected", NEGATIVES["depth"][1], "0x80004005"), 20, "depth")
        result = records("depth")
        result[5]["status"] = "fail"
        self.invalid(finalize(result, "negative_control_rejected", NEGATIVES["depth"][1], "0x80004005"), 20, "depth")
        result = records("upload")
        result[-2]["status"] = "pass"
        self.invalid(result, 20, "upload")
        result = records("upload")
        result.insert(-1, copy.deepcopy(records()[7]))
        self.invalid(result, 20, "upload")

    def test_module_schema_order_completeness_and_path_bounds(self):
        for index in range(1, 5):
            result = records()
            del result[index]
            self.invalid(result)
            result = records()
            result[index]["name"] = MODULES[(index + 1) % 4]
            self.invalid(result)
        for value in ("", "x" * 4097, "bad\u0000path", "bad\x7fpath", "\ud800", None):
            result = records()
            result[1]["path"] = value
            self.invalid(result)
        result = records()
        result[4], result[5] = result[5], result[4]
        self.invalid(result)

    def test_pass_requires_success_detail_hresult_and_exact_schema(self):
        for index in (5, 11, 12):
            for field, value in (("detail", "wrong_golden"), ("hr", "0x80004005"), ("hr", "0x0")):
                result = records()
                result[index][field] = value
                self.invalid(result)
        for index in (0, 1, 5, 12):
            result = records()
            result[index]["extra"] = "unknown field"
            self.invalid(result)
        result = records()
        result[0]["probe"] = "gpu-wave"
        self.invalid(result)
        for field in ("detail", "hr", "non_stage_checks"):
            result = records()
            del result[-1][field]
            self.invalid(result)

    def test_malformed_json_stdout_caps_and_stderr_contamination(self):
        data = encode(records())
        malformed = [b"", b"PASS\n", b"\n" + data, b"wine: warning\n" + data, data + b"PASS\n",
                     encode(records()[:-1]), data + b"{}\n", b"\xff\n{}\n",
                     b"x" * (analyze.MAX_BYTES + 1), b"x" * (analyze.MAX_LINE_BYTES + 1) + b"\n{}\n",
                     data.replace(b'"schema": 1', b'"schema": true', 1),
                     data.replace(b'"schema": 1', b'"schema": 1, "schema": 1', 1),
                     data.replace(b'"checks": 9', b'"checks": NaN', 1),
                     data.replace(b'"checks": 9', b'"checks": Infinity', 1),
                     b"[" * 2000 + b"]" * 2000 + b"\n{}\n"]
        for candidate in malformed:
            with self.subTest(length=len(candidate)):
                with self.assertRaises(analyze.InvalidReport):
                    analyze.validate(candidate, 0)
        for code in (True, 2, 10, 20, 124, -9):
            self.invalid(records(), code)
        for mode in ("pixel", "query", [], None):
            self.invalid(records(), 0, mode)

    def test_genuine_failure_is_reported_but_not_promoted(self):
        result = finalize(records()[:1], "fail", "hardware_device_create", "0x80004005")
        self.assertEqual(analyze.validate(encode(result), 10)["status"], "fail")
        self.invalid(result, 0)
        result = records()
        result[8]["status"] = "fail"
        self.invalid(finalize(result))

    def test_cli_accepts_only_pass_and_exact_negative_not_failure(self):
        with tempfile.TemporaryDirectory(prefix="initializer-parser-") as directory:
            stdout = Path(directory) / "stdout.jsonl"
            cases = [(records(), 0, "none", 0, "pass"),
                     (records("upload"), 20, "upload", 0, "negative_control_rejected"),
                     (records("depth"), 20, "depth", 0, "negative_control_rejected"),
                     (finalize(records()[:1], "fail"), 10, "none", 1, "fail"),
                     (records(), 124, "none", 1, "invalid")]
            for result, code, mode, expected, status in cases:
                stdout.write_bytes(encode(result))
                completed = subprocess.run([sys.executable, "-B", str(Path(analyze.__file__)), "--stdout", str(stdout),
                                            "--exit-code", str(code), "--negative-control", mode],
                                           capture_output=True, text=True, timeout=5)
                self.assertEqual(completed.returncode, expected, completed.stderr)
                self.assertEqual(json.loads(completed.stdout)["status"], status)


if __name__ == "__main__":
    unittest.main()
