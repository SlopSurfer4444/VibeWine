#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Validate captured gpu-wave stdout only. Never launch a guest or consume stderr."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re

MAX_BYTES = 64 * 1024
MAX_LINE_BYTES = 8192
MAX_RECORDS = 11
STAGES = ("draw_basic", "mrt_depth", "compute", "msaa", "query_later_chunk")
MODULES = {"d3d11.dll", "dxgi.dll", "winemetal.dll", "d3dcompiler_47.dll"}
NEGATIVE_STAGE = {"pixel": "draw_basic", "compute": "compute", "query": "query_later_chunk"}


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
        return len(value.encode("utf-8")) <= limit and all(ord(c) >= 32 for c in value)
    except UnicodeError:
        return False


def validate(data: bytes, exit_code: int, negative_control: str = "none") -> dict:
    require(negative_control in {"none", *NEGATIVE_STAGE}, "unknown expected negative control")
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
            and start["probe"] == "gpu-wave" and start["negative_control"] == negative_control,
            "start record or expected negative control mismatch")
    summary_fields = {"schema", "type", "status", "stages_passed", "stages_unsupported", "checks", "non_stage_checks",
                      "negative_controls_rejected"}
    require(summary_fields <= set(summary) <= summary_fields | {"detail", "hr"}
            and summary["type"] == "summary", "missing/faulty final summary")
    if "detail" in summary:
        require(bounded_text(summary["detail"], 2048), "invalid summary detail")
    if "hr" in summary:
        require(isinstance(summary["hr"], str) and re.fullmatch(r"0x[0-9a-fA-F]{8}", summary["hr"]) is not None,
                "invalid summary HRESULT")
    for key in ("stages_passed", "stages_unsupported", "checks", "non_stage_checks", "negative_controls_rejected"):
        require(uint(summary[key]), "summary counters require bounded unsigned integers")
    modules = {}
    stages = []
    for record in records[1:-1]:
        if record.get("type") == "module":
            require(not stages and set(record) == {"schema", "type", "name", "path"}, "module out of order/schema")
            name = record["name"]
            require(isinstance(name, str) and name in MODULES and name not in modules, "unknown/duplicate module")
            require(bounded_text(record["path"], 4096), "invalid module path")
            modules[name] = record["path"]
        elif record.get("type") == "stage":
            fields = set(record)
            require(fields in ({"schema", "type", "name", "status", "checks", "detail"},
                               {"schema", "type", "name", "status", "checks", "detail", "hr"}), "stage schema")
            require(len(stages) < len(STAGES) and record["name"] == STAGES[len(stages)], "missing/duplicate/out-of-order stage")
            require(record["status"] in ("pass", "fail", "unsupported") and uint(record["checks"]), "invalid stage status/checks")
            require(bounded_text(record["detail"], 2048), "invalid stage detail")
            if "hr" in record:
                require(isinstance(record["hr"], str) and re.fullmatch(r"0x[0-9a-fA-F]{8}", record["hr"]) is not None,
                        "invalid HRESULT")
            require(record["status"] != "pass" or record["checks"] > 0, "passing stage without checks")
            stages.append(record)
        else:
            raise InvalidReport("unexpected stdout record type")
    passed = sum(stage["status"] == "pass" for stage in stages)
    unsupported = sum(stage["status"] == "unsupported" for stage in stages)
    failed = [stage for stage in stages if stage["status"] == "fail"]
    checks = sum(stage["checks"] for stage in stages) + summary["non_stage_checks"]
    require(summary["stages_passed"] == passed and summary["stages_unsupported"] == unsupported
            and summary["checks"] == checks, "summary counters disagree with stage evidence")
    status = summary["status"]
    require(isinstance(status, str), "invalid summary status")
    if status in ("pass", "negative_control_rejected"):
        require(set(modules) == MODULES and all(modules.values()), "successful outcome requires all module paths")
    if status == "pass":
        require(exit_code == 0 and negative_control == "none" and len(stages) == len(STAGES), "pass requires normal exit and every stage")
        require(all(stage["status"] == "pass" or (stage["name"] == "msaa" and stage["status"] == "unsupported")
                    for stage in stages), "mandatory stage not passed")
        require(summary["negative_controls_rejected"] == 0, "unexpected negative control rejection")
    elif status == "negative_control_rejected":
        target = NEGATIVE_STAGE.get(negative_control)
        require(exit_code == 20 and target is not None and summary["negative_controls_rejected"] == 1,
                "negative control requires expected mode, exit 20, and exactly one rejection")
        require(len(failed) == 1 and failed[0] is stages[-1] and failed[0]["name"] == target
                and failed[0]["checks"] > 0, "negative control did not reach/fail its intended check")
        require(all(stage["status"] == "pass" or stage is failed[0] or
                    (stage["name"] == "msaa" and stage["status"] == "unsupported") for stage in stages),
                "negative control has unrelated unsupported/failing stages")
    elif status == "unsupported":
        stage_unsupported = any(stage["status"] == "unsupported" and stage["name"] != "msaa" for stage in stages)
        device_unsupported = not stages and bool(summary.get("detail")) and summary.get("hr", "0x00000000") != "0x00000000"
        require(exit_code == 2 and not failed and (stage_unsupported or device_unsupported),
                "unsupported requires mandatory capability/blocker evidence and exit 2")
        require(summary["negative_controls_rejected"] == 0, "unexpected rejection count")
    elif status == "fail":
        require(exit_code == 10 and summary["negative_controls_rejected"] == 0, "failure requires exit 10 and no claimed negative rejection")
    else:
        raise InvalidReport("unknown summary status")
    return {"schema": 1, "status": status, "exit_code": exit_code, "negative_control": negative_control,
            "stages_passed": passed, "stages_unsupported": unsupported, "checks": checks,
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
