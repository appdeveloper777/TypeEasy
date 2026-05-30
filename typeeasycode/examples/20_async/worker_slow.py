#!/usr/bin/env python3
"""Slow echo worker for the TypeEasy async demo.

Protocol (one JSON object per line, both directions):
  request : {"op": "sleep", "ms": <int>, "payload": <any>}
  reply   : {"ok": true, "result": <payload>}

It sleeps `ms` milliseconds before replying, simulating slow I/O (a DB query,
an HTTP call, a heavy computation in another runtime, ...). Running several of
these concurrently via lang_call_async + await_all is what demonstrates that
the TypeEasy event loop overlaps the waits instead of serialising them.
"""
import sys
import json
import time


def main():
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

        op = req.get("op")
        if op == "sleep":
            ms = int(req.get("ms", 0))
            time.sleep(ms / 1000.0)
            reply = {"ok": True, "result": req.get("payload")}
        else:
            reply = {"ok": False, "error": "unknown op: %s" % op}

        sys.stdout.write(json.dumps(reply) + "\n")
        sys.stdout.flush()  # CRITICAL: the bridge reads line-by-line


if __name__ == "__main__":
    main()
