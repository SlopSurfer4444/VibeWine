#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Validate captured initializer guest stdout only; never launch a guest.

Adapted from tests/vibewine/gpu-wave/analyze.py. All seven stages are mandatory.
Only normal pass and the two exact intentional rejections are accepted outcomes.
Source/runtime identity, actual exit capture and GPU launch remain root-owned.
Input is bounded to 64 KiB/13 records; this reader creates no retained artifacts.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re

MAX_BYTES = 64 * 1024
MAX_LINE_BYTES = 8192
MAX_RECORDS = 13
PROBE = "initializer-transaction-gpu"
STAGES = ("private_buffer_zero", "padded_upload_subresources", "zero_texture_growth",
          "render_target_zero", "depth_stencil_zero", "depth_stencil_upload", "padded_volume_upload")
MODULES = ("d3d11.dll", "dxgi.dll", "winemetal.dll", "d3dcompiler_47.dll")
NEGATIVE_STAGE = {"upload": "padded_upload_subresources", "depth": "depth_stencil_upload"}
NEGATIVE_DETAIL = {"upload": "negative_initializer_upload_golden", "depth": "negative_initializer_depth_query"}
SUMMARY_FIELDS = {"schema", "type", "status", "stages_passed", "stages_unsupported", "checks",
                  "non_stage_checks", "negative_controls_rejected", "detail", "hr"}


class InvalidReport(ValueError):
    pass


def require(value: bool, reason: str) -> None:
    if not value:
        raise InvalidReport(reason)


def unique_object(pairs: list[tuple]) -> dict:
    result = {}
    for key, value in pairs:
        require(key not in result, "duplicate JSON key")
        result[key] = value
    return result


def reject_constant(value: str) -> None:
    raise InvalidReport(f"non-finite JSON constant {value}")


def uint(value: object) -> bool:
    return type(value) is int and 0 <= value <= 1_000_000


def bounded_text(value: object, limit: int) -> bool:
    if not isinstance(value, str):
        return False
    try:
        return len(value.encode("utf-8")) <= limit and all(ord(c) >= 32 and ord(c) != 127 for c in value)
    except UnicodeError:
        return False


def hresult(value: object) -> bool:
    return isinstance(value, str) and re.fullmatch(r"0x[0-9a-f]{8}", value) is not None


def validate(data: bytes, exit_code: int, negative_control: str = "none") -> dict:
    require(isinstance(negative_control, str) and negative_control in ("none", *NEGATIVE_STAGE),
            "unknown expected negative control")
    require(type(exit_code) is int, "exit code must be an integer")
    require(isinstance(data, bytes) and 0 < len(data) <= MAX_BYTES, "stdout missing or exceeds byte cap")
    lines = data.splitlines()
    require(2 <= len(lines) <= MAX_RECORDS, "record count outside cap or incomplete report")
    records = []
    for line in lines:
        require(0 < len(line) <= MAX_LINE_BYTES, "empty or oversized record")
        try:
            record = json.loads(line.decode("utf-8", errors="strict"), object_pairs_hook=unique_object,
                                parse_constant=reject_constant)
        except (ValueError, UnicodeError, RecursionError) as error:
            raise InvalidReport("malformed JSON stdout record") from error
        require(isinstance(record, dict) and type(record.get("schema")) is int and record["schema"] == 1,
                "record requires schema integer 1")
        records.append(record)
    start, summary = records[0], records[-1]
    require(set(start) == {"schema", "type", "probe", "negative_control"} and start["type"] == "start"
            and start["probe"] == PROBE and start["negative_control"] == negative_control,
            "start record or expected negative control mismatch")
    require(set(summary) == SUMMARY_FIELDS and summary["type"] == "summary", "missing/faulty final summary")
    require(bounded_text(summary["detail"], 2048) and hresult(summary["hr"]), "invalid summary detail/HRESULT")
    for key in ("stages_passed", "stages_unsupported", "checks", "non_stage_checks", "negative_controls_rejected"):
        require(uint(summary[key]), "summary counters require bounded unsigned integers")
    modules = {}
    stages = []
    for record in records[1:-1]:
        if record.get("type") == "module":
            require(not stages and set(record) == {"schema", "type", "name", "path"}, "module out of order/schema")
            require(len(modules) < len(MODULES) and record["name"] == MODULES[len(modules)],
                    "missing/duplicate/out-of-order module")
            require(bounded_text(record["path"], 4096) and bool(record["path"]), "invalid/empty module path")
            modules[record["name"]] = record["path"]
        elif record.get("type") == "stage":
            require(len(modules) == len(MODULES), "stage before complete module evidence")
            require(set(record) == {"schema", "type", "name", "status", "checks", "detail", "hr"}, "stage schema")
            require(len(stages) < len(STAGES) and record["name"] == STAGES[len(stages)],
                    "missing/duplicate/out-of-order stage")
            require(not stages or stages[-1]["status"] == "pass", "stage after failed stage")
            require(record["status"] in ("pass", "fail") and uint(record["checks"]),
                    "unsupported/invalid stage status or checks")
            require(bounded_text(record["detail"], 2048) and hresult(record["hr"]), "invalid stage detail/HRESULT")
            if record["status"] == "pass":
                require(record["checks"] > 0 and record["detail"] == "verified" and record["hr"] == "0x00000000",
                        "passing stage lacks successful check evidence")
            stages.append(record)
        else:
            raise InvalidReport("unexpected stdout record type")
    passed = sum(stage["status"] == "pass" for stage in stages)
    failed = [stage for stage in stages if stage["status"] == "fail"]
    checks = sum(stage["checks"] for stage in stages) + summary["non_stage_checks"]
    require(summary["stages_passed"] == passed and summary["stages_unsupported"] == 0
            and summary["checks"] == checks, "summary counters disagree with stage evidence")
    status = summary["status"]
    require(isinstance(status, str), "invalid summary status")
    if status == "pass":
        require(exit_code == 0 and negative_control == "none" and len(records) == MAX_RECORDS
                and len(stages) == len(STAGES) and passed == len(STAGES),
                "pass requires normal exit and all seven stages")
        require(summary["negative_controls_rejected"] == 0 and summary["detail"] == "complete"
                and summary["hr"] == "0x00000000", "invalid successful summary")
    elif status == "negative_control_rejected":
        target = NEGATIVE_STAGE.get(negative_control)
        detail = NEGATIVE_DETAIL.get(negative_control)
        require(exit_code == 20 and target is not None and summary["negative_controls_rejected"] == 1,
                "negative control requires expected mode, exit 20 and exactly one rejection")
        require(len(failed) == 1 and failed[0] is stages[-1] and failed[0]["name"] == target
                and failed[0]["checks"] > 0, "negative control did not reach/fail its intended check")
        require(failed[0]["detail"] == detail and summary["detail"] == detail
                and failed[0]["hr"] == summary["hr"] == "0x80004005",
                "negative control detail/HRESULT does not identify the intended golden")
    elif status == "fail":
        require(exit_code == 10 and summary["negative_controls_rejected"] == 0,
                "failure requires exit 10 and no claimed negative rejection")
    else:
        raise InvalidReport("unknown/unsupported summary status")
    return {"schema": 1, "status": status, "exit_code": exit_code, "negative_control": negative_control,
            "stages_passed": passed, "stages_unsupported": 0, "checks": checks,
            "non_stage_checks": summary["non_stage_checks"],
            "negative_controls_rejected": summary["negative_controls_rejected"],
            "modules": modules, "stages": stages, "evidence": "captured_guest_stdout_only"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stdout", type=Path, required=True, help="captured guest stdout, never merged stderr")
    parser.add_argument("--exit-code", type=int, required=True, help="actual guest process exit code")
    parser.add_argument("--negative-control", choices=("none", *NEGATIVE_STAGE), default="none")
    args = parser.parse_args()
    try:
        with args.stdout.open("rb") as stream:
            result = validate(stream.read(MAX_BYTES + 1), args.exit_code, args.negative_control)
    except (OSError, InvalidReport) as error:
        print(json.dumps({"schema": 1, "status": "invalid", "reason": str(error)[:2048]}))
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0 if result["status"] in ("pass", "negative_control_rejected") else 1


if __name__ == "__main__":
    raise SystemExit(main())
