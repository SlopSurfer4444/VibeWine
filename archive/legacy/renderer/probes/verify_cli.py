"""Subprocess CLI assertions only; this never reads or mutates a shared mapping."""
import json
import pathlib
import subprocess

base = pathlib.Path(__file__).resolve().parent
receipt = json.loads((base / "test-results.json").read_text())
binary = str(base / "cs2trace")
file = receipt["cli_fixture"]
identity = receipt["cli_identity"]
checks = 0

def run(args, status):
    global checks
    result = subprocess.run([binary, *args], capture_output=True, text=True)
    assert result.returncode == status, (args, result.returncode, result.stderr)
    checks += 1
    return result

inspection = json.loads(run(["inspect", file], 0).stdout)
assert inspection["identity"] == identity and inspection["capture"] == 0
for generation in ["0", "-1", "4294967296", "1844674407370955161500000", "1x", ""]:
    run(["arm", file, generation, identity], 1)
run(["arm", file, "31", identity], 1)  # Existing committed generation.
run(["arm", file, "32", "0:0"], 1)
run(["stop", file, "32", identity], 1)
rows = [json.loads(line) for line in run(["decode", file, "31", identity], 0).stdout.splitlines()]
assert len(rows) == 5001 and rows[-1]["events"] == 5000
assert rows[0]["padding0"] == 500 and rows[0]["padding1"] == 700
assert rows[0]["clock_domain"] == "native_gpu_ns" and rows[0]["duration_ns"] == 100
assert rows[-1]["bridge_verified"] is False and rows[-1]["live_pid_verified"] is False
run(["arm", file, "32", identity], 0)
run(["decode", file, "32", identity], 1)
run(["stop", file, "33", identity], 1)
run(["stop", file, "32", identity], 0)
run(["arm", file, "32", identity], 1)  # Empty generation reservation persists.
run(["decode", file, "32", identity], 0)
run(["decode", file, "31"], 2)
invalid_gpu = [json.loads(line) for line in run(["decode", receipt["cli_invalid_gpu_fixture"], "7", receipt["cli_invalid_gpu_identity"]], 3).stdout.splitlines()]
assert invalid_gpu[0]["flags"] == 8 and invalid_gpu[0]["duration_ns"] is None
(base / "cli-test-results.json").write_text(json.dumps({"result": "pass", "cli_checks": checks, "mutable_mapping_reads_in_python": False}) + "\n")
print(f"CLI: {checks} subprocess assertions passed")
