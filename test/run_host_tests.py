#!/usr/bin/env python3
"""Build host suites and run each case in a separate process (no dependencies)."""

import argparse
from collections import Counter
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


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
    compiler = shlex.split(os.environ.get("CXX", "g++"))
    counts: Counter = Counter()

    # Build products never enter the source tree or PlatformIO's build folder.
    with tempfile.TemporaryDirectory(prefix="entime-host-tests-") as build_dir:
        for index, source in enumerate(suites):
            executable = Path(build_dir) / f"suite-{index}"
            command = compiler + [
                "-std=c++17", "-O1", "-g", "-Wall", "-Wextra",
            ]
            extra_sources = []
            if source.parent.name == "project":
                # Keep broader Arduino/HAL doubles isolated from timing suites.
                command += [
                    f"-I{source.parent / 'stubs'}",
                    "-isystem", str(test_dir / "vendor" / "ArduinoJson" / "src"),
                    "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                ]
                if source.name == "config_command_test.cpp":
                    extra_sources = [
                        repo_dir / "src" / relative for relative in (
                            "storage/settings.cpp", "command/handlers.cpp",
                            "hal/touch/touch.cpp",
                        )
                    ]
                elif source.name == "ble_delivery_test.cpp":
                    extra_sources = [
                        repo_dir / "src" / relative for relative in (
                            "hal/ble/nus_service.cpp", "timing/event_isr.cpp",
                            "timing/event_dispatcher.cpp", "timing/event_timestamp.cpp",
                        )
                    ]
                elif source.name == "command_parser_test.cpp":
                    extra_sources = [repo_dir / "src" / relative for relative in (
                        "command/command_parser.cpp", "hal/ble/nus_service.cpp",
                    )]
            command += [
                f"-I{test_dir / 'host' / 'stubs'}", f"-I{repo_dir / 'src'}",
            ]
            if source.parent.name == "project":
                extra_sources.append(source.parent / "clock.cpp")
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
