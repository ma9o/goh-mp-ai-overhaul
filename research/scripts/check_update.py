#!/usr/bin/env python3
"""Audit a new game/editor build without executing it or installing hooks.

Exit 0: required static contracts passed (NOT runtime approval).
Exit 1: required contracts failed/unresolved, or --strict found any unchecked item.
Exit 2: invalid profile, invocation or report-output failure.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

try:
    from compatibility import Audit, GOOD, markdown, sha
except ImportError as error:
    raise SystemExit(str(error)) from error


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", type=Path, default=Path(__file__).resolve().parents[1] /
                        "artifacts/build-25492057/compatibility-profile.json")
    parser.add_argument("--binaries-dir", required=True, type=Path)
    parser.add_argument("--build-id", required=True)
    parser.add_argument("--resources-dir", type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--strict", action="store_true", help="Also fail on optional/uncovered mappings")
    args = parser.parse_args()
    if not args.build_id.isdecimal():
        parser.error("--build-id must contain decimal digits only")
    try:
        encoded = args.profile.read_bytes()
        profile = json.loads(encoded)
        report = Audit(profile, args.binaries_dir, args.resources_dir).run(args.build_id)
        report["profile_sha256"] = sha(encoded)
        report["strict"] = args.strict
        failed = not report["required_checks_passed"]
        if args.strict:
            failed |= any(r["status"] not in GOOD for r in report["mappings"])
        report["exit_code"] = int(failed)
        args.output_dir.mkdir(parents=True, exist_ok=True)
        files = [("report.json", json.dumps(report, indent=2) + "\n"), ("report.md", markdown(report))]
        for key in profile["binaries"]:
            candidates = {}
            for row in report["mappings"]:
                if (row["kind"] == "function" and row.get("binary") == key
                        and row["status"] in GOOD | {"CONTRACT_CHANGED"} and "rva" in row["current"]):
                    candidates.setdefault(row["current"]["rva"], []).append(f"{row['id']}:{row['status']}")
            body = "# RVA\tInspection candidates only; NOT runtime-approved hook targets\n"
            body += "".join(f"{rva}\t{' | '.join(labels)}\n" for rva, labels in
                            sorted(candidates.items(), key=lambda pair: int(pair[0], 16)))
            files.append((f"{key}.candidates.tsv", body))
        for name, text in files:
            target = args.output_dir / name
            if target.resolve() == args.profile.resolve():
                raise ValueError("Report must not overwrite its input profile")
            temporary = target.with_suffix(target.suffix + ".tmp")
            temporary.write_text(text)
            temporary.replace(target)
    except (ValueError, KeyError, TypeError, OSError) as error:
        parser.exit(2, f"Audit could not run: {error}\n")
    print(", ".join(f"{key}={value}" for key, value in sorted(report["summary"].items())))
    for row in report["mappings"]:
        if row["status"] not in GOOD and row["required"]:
            print(f"{row['status']}: {row['id']}: {'; '.join(row['reasons'])}")
    print(f"Required static contracts: {'PASSED' if report['required_checks_passed'] else 'FAILED'}; "
          f"audit exit={int(failed)}; runtime approval: BLOCKED")
    print(f"Reports: {args.output_dir / 'report.json'} and {args.output_dir / 'report.md'}")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
