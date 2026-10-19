#!/usr/bin/env python3
"""Build host suites and run each case in a separate process (no dependencies)."""

import argparse
from collections import Counter
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


# Translation units linked with suites that do not include production sources
# directly. Paths are relative to src/; shared support is configured separately.
PRODUCTION_SOURCES = {
    "events/ble_delivery_test.cpp": (
        "hal/ble/nus_service.cpp", "timing/event_isr.cpp",
        "timing/event_dispatcher.cpp", "timing/event_timestamp.cpp",
    ),
    "events/event_timestamp_test.cpp": (
        "storage/settings.cpp", "timing/event_timestamp.cpp",
        "timing/event_dispatcher.cpp", "timing/event_isr.cpp", "hal/ble/nus_service.cpp",
    ),
    "events/event_pipeline_test.cpp": (
        "storage/settings.cpp", "timing/event_timestamp.cpp", "timing/event_dispatcher.cpp",
        "timing/event_isr.cpp", "timing/pps_isr.cpp", "timing/rtc_sqw_isr.cpp",
        "hal/ble/nus_service.cpp",
    ),
    "settings/settings_persistence_test.cpp": (),
    "settings/settings_validation_test.cpp": (),
    "commands/config_command_test.cpp": (
        "storage/settings.cpp", "command/handlers.cpp", "hal/touch/touch.cpp",
    ),
    "commands/command_parser_test.cpp": (
        "command/command_parser.cpp", "hal/ble/nus_service.cpp",
    ),
    "commands/command_router_test.cpp": (
        "storage/settings.cpp", "command/handlers.cpp", "command/command_parser.cpp",
        "command/command_router.cpp", "hal/touch/touch.cpp", "runtime/build_info.cpp",
    ),
    "gps/gps_driver_test.cpp": ("hal/gps/gps.cpp",),
    "rtc/rtc_driver_test.cpp": ("hal/rtc/rtc.cpp",),
    "touch/touch_driver_test.cpp": ("hal/touch/touch.cpp",),
    "touch/touch_flow_test.cpp": (
        "app/touch_calibration_service.cpp", "hal/touch/touch.cpp", "storage/settings.cpp",
    ),
    "battery/battery_driver_test.cpp": ("hal/ina226/ina226.cpp",),
}
SUPPORT_SOURCES = {
    "events/event_pipeline_test.cpp": ("hal_globals.cpp",),
    "commands/command_router_test.cpp": ("hal_globals.cpp", "command_environment.cpp"),
}
DRIVER_GROUPS = {"gps", "rtc", "touch", "battery"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true", help="enable UBSan")
    parser.add_argument(
        "--strict", action="store_true", help="fail on known issues as well"
    )
    parser.add_argument("--filter", default="", help="run names containing this text")
    parser.add_argument("--suite", default="", help="build suite paths containing this text")
    args = parser.parse_args()
    test_dir = Path(__file__).resolve().parent
    repo_dir = test_dir.parent
    suites = [source for source in sorted((test_dir / "host").glob("**/*_test.cpp"))
              if args.suite in str(source.relative_to(test_dir))]
    host_dir = test_dir / "host"
    shared = host_dir / "support" / "application"
    compiler = shlex.split(os.environ.get("CXX", "g++"))
    counts: Counter = Counter()

    # Build products never enter the source tree or PlatformIO's build folder.
    with tempfile.TemporaryDirectory(prefix="entime-host-tests-") as build_dir:
        for index, source in enumerate(suites):
            executable = Path(build_dir) / f"suite-{index}"
            command = compiler + [
                "-std=c++17", "-O1", "-g", "-Wall", "-Wextra",
            ]
            suite_path = source.relative_to(host_dir).as_posix()
            group = suite_path.split("/", 1)[0]
            extra_sources = []
            if group != "timing":
                if suite_path not in PRODUCTION_SOURCES:
                    parser.error(f"no build configuration for {suite_path}")
                # Hardware API doubles override application-level HAL models.
                if group in DRIVER_GROUPS:
                    command += [f"-I{host_dir / 'stubs' / 'drivers'}"]
                command += [
                    f"-I{host_dir / 'stubs' / 'application'}",
                    "-isystem", str(test_dir / "vendor" / "ArduinoJson" / "src"),
                    "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                ]
                extra_sources += [repo_dir / "src" / name
                                  for name in PRODUCTION_SOURCES[suite_path]]
                extra_sources += [shared / name
                                  for name in SUPPORT_SOURCES.get(suite_path, ())]
                extra_sources.append(shared / "clock.cpp")
                if group == "gps":
                    command += ["-isystem", str(test_dir / "vendor" / "MicroNMEA" / "src")]
                    extra_sources.append(test_dir / "vendor/MicroNMEA/src/MicroNMEA.cpp")
            command += [
                f"-I{host_dir / 'stubs' / 'base'}", f"-I{repo_dir / 'src'}",
            ]
            if args.sanitize:
                command += ["-fsanitize=undefined", "-fno-sanitize-recover=all"]
            command += [str(source), *map(str, extra_sources), "-o", str(executable)]
            print(f"BUILD {source.relative_to(repo_dir)}", flush=True)
            subprocess.run(command, check=True, timeout=120)
            listing = subprocess.run(
                [str(executable), "--list"], check=True, capture_output=True,
                text=True, timeout=10,
            )
            for line in listing.stdout.splitlines():
                name, issue = line.split("\t")
                if args.filter not in name:
                    continue
                result = subprocess.run(
                    [str(executable), name], capture_output=True, text=True, timeout=10
                )
                known_failure = issue != "-"
                if result.returncode == 0:
                    status = "XPASS" if known_failure else "PASS"
                elif result.returncode == 10:
                    status = "XFAIL" if known_failure else "FAIL"
                else:
                    # Crashes, sanitizer errors and setup errors are never XFAIL.
                    status = "ERROR"
                counts[status] += 1
                marker = f" ({issue})" if known_failure else ""
                print(f"{status:5} {name}{marker}", flush=True)
                output = (result.stdout + result.stderr).strip()
                if output:
                    print(f"      {output}", flush=True)
                if status == "XPASS":
                    print("      Remove the known_issue marker after reviewing the fix.")

    if not counts:
        parser.error("no matching test cases")
    print("\n" + ", ".join(f"{name}={count}" for name, count in sorted(counts.items())))
    return int(
        bool(counts["FAIL"] or counts["ERROR"] or counts["XPASS"])
        or (args.strict and bool(counts["XFAIL"]))
    )


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, subprocess.SubprocessError) as error:
        print(f"ERROR: {error}")
        raise SystemExit(2)
