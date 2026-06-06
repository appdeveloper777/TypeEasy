#!/bin/sh
# POSIX-sh sleep worker for the async OVERLAP test in Docker/Linux, where the
# slim runtime image ships no python3. Same line protocol as sleep_worker.py:
# read one request line, sleep ~500 ms (simulating slow I/O), then echo the
# request back (it carries the {payload} token the test asserts on).
while IFS= read -r line; do
    [ -z "$line" ] && continue
    sleep 0.5
    printf '%s\n' "$line"
done
