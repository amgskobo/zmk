#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run actual Studio lock/discovery/transport sources in a real native Zephyr kernel."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]
FIXTURE = Path(__file__).resolve().parent
BAD = re.compile(r"runtime error:|AddressSanitizer:|UndefinedBehaviorSanitizer:|ASSERTION FAIL|ZEPHYR FATAL ERROR")
COMPLETE = "STUDIO_DISCOVERY_PASS cases=12"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def execute(command, output, env, timeout=180):
    started = time.time_ns()
    tick = time.monotonic_ns()
    result = subprocess.run(command, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=timeout)
    output.with_suffix(".log").write_bytes(result.stdout)
    output.with_suffix(".json").write_text(json.dumps({
        "argv": list(map(str, command)), "start_unix_ns": started,
        "elapsed_ns": time.monotonic_ns() - tick, "exit": result.returncode,
    }, indent=2) + "\n")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zephyr", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--original-core", required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9a-f]{40}", args.original_core):
        parser.error("original core must be a full local commit")
    output = args.output.parent.resolve(strict=True) / args.output.name
    if os.path.lexists(output):
        parser.error("output must be fresh, including symlinks")
    output.mkdir()
    inputs = [ROOT / path for path in (
        "app/src/studio/core.c", "app/src/studio/ble_discovery.c",
        "app/src/studio/gatt_rpc_transport.c", "app/src/studio/Kconfig",
        "app/src/studio/rpc.c", "app/src/ble.c", "app/src/behaviors/behavior_studio_unlock.c",
        "app/include/zmk/studio/core.h", "app/include/zmk/studio/ble_discovery.h",
    )] + sorted(path for path in FIXTURE.rglob("*") if path.is_file())
    before = {str(p.relative_to(ROOT)): sha(p) for p in inputs}
    env = dict(os.environ, ZEPHYR_BASE=str(args.zephyr),
               Zephyr_DIR=str(args.zephyr / "share/zephyr-package/cmake"),
               CMAKE_BUILD_PARALLEL_LEVEL="2", PYTHONDONTWRITEBYTECODE="1",
               ASAN_OPTIONS="detect_leaks=0:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1")
    original = subprocess.check_output(
        ["git", "-c", "safe.directory=" + str(ROOT), "show",
         args.original_core + ":app/src/studio/core.c"], cwd=ROOT, env=env)
    control = output / "original-core.c"
    control.write_bytes(original)
    report = {"source_before": before, "original_commit": args.original_core,
              "original_source_sha256": sha(control), "variants": []}
    report["zephyr_head"] = subprocess.check_output(
        ["git", "-C", str(args.zephyr), "rev-parse", "HEAD"], text=True, env=env).strip()
    try:
        fixed = ROOT / "app/src/studio/core.c"
        for name, source, options in (
                ("original-control", control, []), ("fixed", fixed, []),
                ("discovery-off", fixed, ["-DSTUDIO_DISCOVERY_ENABLED=OFF"]),
                ("timeout-zero", fixed, ["-DSTUDIO_IDLE_TIMEOUT_SEC=0"])):
            lane = output / name
            lane.mkdir()
            build = lane / "build"
            configure = ["cmake", "-GNinja", "-S", str(FIXTURE), "-B", str(build),
                         "-DBOARD=native_sim/native/64", "-DWEST=OFF", "-DZEPHYR_MODULES=",
                         "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", "-DEXTRA_CFLAGS=-fno-sanitize-recover=all",
                         "-DSTUDIO_CORE_SOURCE=" + str(source), *options]
            for label, command in (("configure", configure), ("build", ["cmake", "--build", str(build)])):
                result = execute(command, lane / label, env)
                if result.returncode:
                    print(result.stdout.decode(errors="replace")[-7000:], flush=True)
                    raise AssertionError(name + " " + label + " failed; not a runtime control")
            config = (build / "zephyr/.config").read_text().splitlines()
            for required in ("CONFIG_ASSERT=y", "CONFIG_ASAN=y", "CONFIG_UBSAN=y"):
                assert required in config, required
            commands = json.loads((build / "compile_commands.json").read_text())
            report["compiled_sources_" + name] = {
                row["file"]: sha(Path(row["file"])) for row in commands if Path(row["file"]).is_file()
            }
            binary = build / "zephyr/zephyr.exe"
            result = execute([str(binary), "-no-rt"], lane / "run", env)
            raw = result.stdout.decode(errors="replace")
            row = {"name": name, "exit": result.returncode, "binary_sha256": sha(binary),
                   "completed_cases": len(re.findall(r"^STUDIO_CASE_PASS ", raw, re.M))}
            if name == "original-control":
                assert result.returncode != 0 and "REGRESSION: unlock must not directly request advertising" in raw, raw
                row["status"] = "EXPECTED ASSERTION REJECTION"
            else:
                completion = COMPLETE if name == "fixed" else "STUDIO_CONFIG_PASS cases=1"
                assert result.returncode == 0 and completion in raw and not BAD.search(raw), raw
                assert row["completed_cases"] == (12 if name == "fixed" else 1), row
                row["status"] = "PASS"
            report["variants"].append(row)
            print(name + " " + row["status"], flush=True)
    finally:
        report["source_after"] = {str(p.relative_to(ROOT)): sha(p) for p in inputs}
        report["source_drift"] = report["source_before"] != report["source_after"]
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    assert not report["source_drift"], "source drift invalidates run"


if __name__ == "__main__":
    main()
