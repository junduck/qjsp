#!/usr/bin/env python3
"""test262 parser runner — runs every .js file through parser2, records results in SQLite."""

import subprocess
import sys
import os
import re
import sqlite3
import signal
import json
from pathlib import Path
from collections import Counter

PARSER_BIN = os.path.join(os.path.dirname(__file__), "..", "build-debug", "perf_test")
TEST262_ROOT = os.path.join(os.path.dirname(__file__), "..", "refs", "test262")

DB_PATH = os.path.join(os.path.dirname(__file__), "..", "test262_results.db")

SKIP_FEATURES = {"decorators"}

def parse_frontmatter(path):
    """Extract test262 YAML frontmatter. Returns dict or None."""
    try:
        text = path.read_text()
    except Exception:
        return None
    m = re.search(r"/\*---(.*?)---\*/", text, re.DOTALL)
    if not m:
        return None
    yaml_text = m.group(1)

    meta = {"flags": [], "features": [], "negative": None}
    current_key = None
    in_negative = False
    negative_data = {}

    for line in yaml_text.split("\n"):
        colon = line.find(":")
        if colon == -1:
            continue

        key = line[:colon].strip()
        val = line[colon + 1:].strip()

        if in_negative and key not in ("type", "phase"):
            in_negative = False

        if key == "flags":
            meta["flags"] = _parse_array(val)
        elif key == "features":
            meta["features"] = _parse_array(val)
        elif key == "negative":
            in_negative = True
            negative_data = {}
        elif in_negative:
            negative_data[key] = val

    if negative_data:
        meta["negative"] = negative_data

    return meta


def _parse_array(val):
    if val.startswith("[") and val.endswith("]"):
        inner = val[1:-1].strip()
        if not inner:
            return []
        return [x.strip() for x in inner.split(",")]
    return []


def should_skip(meta):
    if meta is None:
        return "no_frontmatter"
    if "raw" in meta["flags"]:
        return "flag:raw"
    if "noStrict" in meta["flags"]:
        return "flag:noStrict"
    for feat in meta["features"]:
        if feat in SKIP_FEATURES:
            return f"feature:{feat}"
    return None


def expected_outcome(meta):
    """Returns 'ok' or 'error'."""
    neg = meta.get("negative")
    if neg and neg.get("phase") == "parse":
        return "error"
    return "ok"


def run_parser(path):
    """Run parser on a .js file. Returns (exit_code, stdout_lines, crashed)."""
    try:
        r = subprocess.run(
            [PARSER_BIN, str(path)],
            capture_output=True, text=True, timeout=1
        )
        return r.returncode, r.stdout.strip().split("\n") if r.stdout.strip() else [], False
    except subprocess.TimeoutExpired:
        return -1, ["TIMEOUT"], True
    except Exception as e:
        return -1, [str(e)], True


def init_db():
    conn = sqlite3.connect(DB_PATH)
    conn.execute("""
        CREATE TABLE IF NOT EXISTS results (
            path TEXT PRIMARY KEY,
            status TEXT,
            expected TEXT,
            errors TEXT,
            error_count INTEGER
        )
    """)
    conn.execute("""
        CREATE TABLE IF NOT EXISTS progress (
            key TEXT PRIMARY KEY,
            value TEXT
        )
    """)
    conn.commit()
    return conn


def main():
    if not os.path.exists(PARSER_BIN):
        print(f"Error: parser binary not found: {PARSER_BIN}")
        print("Build with: cmake --build build-release --target perf_test")
        sys.exit(1)

    test_dir = os.path.join(TEST262_ROOT, "test")
    if not os.path.exists(test_dir):
        print(f"Error: test262 not found: {test_dir}")
        sys.exit(1)

    # Collect all .js files
    print("Collecting files...")
    all_files = sorted(Path(test_dir).rglob("*.js"))
    # Filter out harness, fixtures
    files = [f for f in all_files
             if "harness" not in str(f) and "_FIXTURE" not in f.name]
    print(f"Found {len(files)} .js files (filtered)")

    conn = init_db()

    # Find already-processed files
    done = set(row[0] for row in conn.execute("SELECT path FROM results"))
    print(f"Already processed: {len(done)}")

    pending = [f for f in files if str(f) not in done]
    print(f"Pending: {len(pending)}")

    # tqdm progress bar
    try:
        from tqdm import tqdm
        pbar = tqdm(total=len(pending), unit="file", desc="Parsing")
        use_tqdm = True
    except ImportError:
        pbar = None
        use_tqdm = False
        print("(install tqdm for progress bar: pip install tqdm)")

    counts = Counter()
    last_file = ""

    for f in pending:
        fpath = str(f)
        last_file = fpath

        if not use_tqdm and counts["total"] % 500 == 0:
            print(f"  progress: {counts['total']}/{len(pending)} files", flush=True)

        meta = parse_frontmatter(f)
        skip_reason = should_skip(meta)
        if skip_reason:
            conn.execute(
                "INSERT OR REPLACE INTO results VALUES (?, ?, ?, ?, ?)",
                (fpath, "skipped", skip_reason, "", 0)
            )
            counts["skipped"] += 1
            counts["total"] += 1
            if use_tqdm:
                pbar.update(1)
                pbar.set_postfix(skipped=counts["skipped"], ok=counts["ok"],
                                 err_ok=counts["err_expected"], err_unexpected=counts["err_unexpected"])
            continue

        expected = expected_outcome(meta)
        exit_code, errors, crashed = run_parser(f)

        if crashed:
            print(f"CRASH: {fpath}  ({','.join(errors)})", flush=True)
            conn.commit()
            sys.exit(2)

        counts["total"] += 1

        if exit_code == 0 and expected == "ok":
            status = "ok"
            counts["ok"] += 1
        elif exit_code != 0 and expected == "error":
            status = "err_expected"
            counts["err_expected"] += 1
        elif exit_code != 0 and expected == "ok":
            status = "err_unexpected"
            counts["err_unexpected"] += 1
        elif exit_code == 0 and expected == "error":
            status = "ok_unexpected"
            counts["ok_unexpected"] += 1
        else:
            status = "unknown"

        conn.execute(
            "INSERT OR REPLACE INTO results VALUES (?, ?, ?, ?, ?)",
            (fpath, status, expected, "\n".join(errors), len(errors))
        )

        if use_tqdm:
            pbar.update(1)
            pbar.set_postfix(skipped=counts["skipped"], ok=counts["ok"],
                             err_ok=counts["err_expected"], err_unexpected=counts["err_unexpected"])

        # Commit every 1000 files
        if counts["total"] % 1000 == 0:
            conn.commit()

    conn.commit()

    if use_tqdm:
        pbar.close()

    print(f"\n=== Results ===")
    print(f"  Total:       {counts['total']}")
    print(f"  Skipped:     {counts['skipped']}")
    print(f"  OK:          {counts['ok']}")
    print(f"  Err(expected): {counts['err_expected']}")
    print(f"  Err(unexpected): {counts['err_unexpected']}")
    print(f"  OK(unexpected):  {counts['ok_unexpected']}")
    print(f"  Crashed:     {counts['crashed']}")
    ran = counts["ok"] + counts["err_expected"] + counts["err_unexpected"] + counts["ok_unexpected"]
    if ran > 0:
        passed = counts["ok"] + counts["err_expected"]
        print(f"  Pass rate:   {100*passed/ran:.1f}% (of run)")

    # Show crashes
    crashes = conn.execute(
        "SELECT path, errors FROM results WHERE status = 'crashed' ORDER BY path"
    ).fetchall()
    if crashes:
        print(f"\nCrashed files ({len(crashes)}):")
        for path, errs in crashes:
            print(f"  {path}  ({errs})")

    # Show unexpected failures
    unexpected = conn.execute(
        "SELECT path, errors FROM results WHERE status IN ('err_unexpected','ok_unexpected') LIMIT 50"
    ).fetchall()
    if unexpected:
        print(f"\nUnexpected results (first 50):")
        for path, errs in unexpected:
            first_err = errs.split("\n")[0] if errs else "(no errors)"
            print(f"  [{status}] {path}")
            print(f"         {first_err}")

    conn.close()


if __name__ == "__main__":
    main()
