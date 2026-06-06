#!/usr/bin/env python3
"""Sleep worker for the async OVERLAP test (tests/api/run_async_overlap_test.py).

One JSON object per line, both directions:
  request : {"op": "sleep", "ms": <int>, "payload": <any>}
  reply   : {"ok": true, "result": <payload>}

It sleeps `ms` milliseconds before replying, simulating slow I/O. When several
of these are awaited concurrently across DISTINCT HTTP requests, the Option-1a
cooperative interleaving lets their waits overlap instead of serialising.
"""
import json
import sys
import time


def main() -> None:
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except Exception as exc:  # noqa: BLE001
            sys.stdout.write(json.dumps({"ok": False, "error": str(exc)}) + "\n")
            sys.stdout.flush()
            continue

        if req.get("op") == "sleep":
            ms = int(req.get("ms", 0))
            time.sleep(max(0, ms) / 1000.0)
            reply = {"ok": True, "result": req.get("payload")}
        else:
            reply = {"ok": False, "error": "unknown op: %s" % req.get("op")}

        sys.stdout.write(json.dumps(reply) + "\n")
        sys.stdout.flush()  # CRITICAL: the bridge reads line-by-line


if __name__ == "__main__":
    main()
