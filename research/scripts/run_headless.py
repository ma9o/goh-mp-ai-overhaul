#!/usr/bin/env python3
"""Run local Ghidra imports or read-only evidence exports; never launch the game."""
from __future__ import annotations

import argparse
import os
import re
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-id", required=True)
    parser.add_argument("--ghidra-home", type=Path, default=os.environ.get(
        "GHIDRA_HOME", "/opt/homebrew/opt/ghidra/libexec"))
    parser.add_argument("--java-home", type=Path, default=os.environ.get(
        "JAVA_HOME", "/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home"))
    sub = parser.add_subparsers(dest="stage", required=True)
    importer = sub.add_parser("import", help="Import and analyze a fresh pair of executables")
    importer.add_argument("--binary", type=Path, action="append", required=True)
    importer.add_argument("--timeout", type=int, default=900,
                          help="Autoanalysis timeout per binary, in seconds")
    exporter = sub.add_parser("export", help="Export evidence from an existing analyzed program")
    exporter.add_argument("--program", required=True, help="Imported executable filename")
    exporter.add_argument("--candidates", type=Path, help="Optional additional/alternative candidate TSV")
    exporter.add_argument("--output", type=Path, help="Local output directory (contains decompiled code)")
    args = parser.parse_args()
    if not args.build_id.isdecimal():
        parser.error("Steam build ID must contain decimal digits only")
    repo = Path(__file__).resolve().parents[2]
    work = repo / "local-work"
    project = work / "ghidra"
    logs = work / "logs"
    project.mkdir(parents=True, exist_ok=True)
    logs.mkdir(parents=True, exist_ok=True)
    launcher = args.ghidra_home / "support/analyzeHeadless"
    if not launcher.is_file() or not (args.java_home / "bin/java").is_file():
        parser.error("Ghidra or Java not found; supply --ghidra-home and --java-home")
    command = [str(launcher), str(project), f"GoH_{args.build_id}"]
    if args.stage == "import":
        if any(not p.is_file() for p in args.binary):
            parser.error("An input binary does not exist")
        # No -overwrite: existing program databases must not be silently replaced.
        command += ["-import", *[str(p.resolve()) for p in args.binary],
                    "-max-cpu", "4", "-analysisTimeoutPerFile", str(args.timeout)]
        tag = "import"
    else:
        if Path(args.program).name != args.program:
            parser.error("--program must be an imported filename, not a path")
        stem = Path(args.program).stem
        artifacts = repo / "research/artifacts" / f"build-{args.build_id}"
        survey = artifacts / f"{stem}.pe.json"
        candidates = args.candidates or artifacts / f"{stem}.candidates.tsv"
        if not survey.is_file() or not candidates.is_file():
            parser.error("Run scan_pe.py first, and ensure candidate TSV exists")
        output = args.output or work / "exports" / f"build-{args.build_id}" / stem
        command += ["-process", args.program, "-noanalysis", "-readOnly",
                    "-scriptPath", str(repo / "research/ghidra"),
                    "-postScript", "ExportAiAnchors.java", str(survey),
                    str(candidates.resolve()), str(output.resolve())]
        tag = f"export-{stem}"
    command += ["-log", str(logs / f"{tag}.log"),
                "-scriptlog", str(logs / f"{tag}-scripts.log")]
    env = os.environ.copy()
    env["JAVA_HOME"] = str(args.java_home)
    env["PATH"] = str(args.java_home / "bin") + os.pathsep + env.get("PATH", "")
    env["GHIDRA_HEADLESS_MAXMEM"] = "4G"
    log_path = logs / f"{tag}-console.log"
    print(f"Running {args.stage}; log: {log_path}", flush=True)
    with log_path.open("w") as log:
        result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT)
    text = log_path.read_text(errors="replace")
    print("\n".join(text.splitlines()[-24:]))
    # Ghidra can log a script error with exit code zero. Conversely, automatic
    # analysis can report nonfatal demangler errors and still finish/save.
    if args.stage == "import":
        completed = text.count("REPORT: Import succeeded") == len(args.binary)
        completed &= text.count("REPORT: Analysis succeeded for file:") == len(args.binary)
    else:
        completed = bool(re.search(r"Exported \d+ functions to ", text))
    timed_out = "timed out" in text.lower() or "analysis timeout" in text.lower()
    failed = result.returncode != 0 or not completed or timed_out
    errors = sum(line.startswith("ERROR ") for line in text.splitlines())
    if errors:
        print(f"Ghidra logged {errors} ERROR lines; inspect {log_path} even if the stage completed.")
    if timed_out:
        print("Inspect the analysis log: a timeout may have left partial analysis.")
    raise SystemExit(1 if failed else 0)


if __name__ == "__main__":
    main()
