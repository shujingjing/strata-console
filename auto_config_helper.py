"""One-shot hardware/recommendation helper for Strata Console.

Reads Strata's own MCP implementation (setup.py rules) and prints one JSON object.
The console starts this only when the user presses "auto configure"; it exits at once.
"""
from __future__ import annotations

import argparse
import contextlib
import io
import json
import pathlib
import sys


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    args = ap.parse_args()
    root = pathlib.Path(args.root).resolve()
    sys.path.insert(0, str(root / "tools"))
    try:
        import strata_mcp  # noqa: E402

        # strata_mcp may print; keep stdout clean for the JSON result.
        with contextlib.redirect_stdout(io.StringIO()):
            strata = strata_mcp.Strata(root)
            hardware = strata.hardware()
            recommendation = strata.recommend(hardware)
        version = None
        try:
            build = json.loads((root / "engine" / "BUILD.json").read_text(encoding="utf-8"))
            version = build.get("version")
        except Exception:
            pass
        print(json.dumps({
            "ok": True,
            "hardware": hardware,
            "recommendation": recommendation,
            "engine_version": version,
        }, ensure_ascii=False))
        return 0
    except Exception as exc:  # noqa: BLE001 - the console shows the message
        print(json.dumps({
            "ok": False,
            "error": f"{type(exc).__name__}: {exc}",
        }, ensure_ascii=False))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
