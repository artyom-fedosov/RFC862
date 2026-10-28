#!/bin/sh
# Smoke test for a release binary inside a minimal container: echo over TCP and UDP
# on port 7, then a clean exit on SIGTERM. Needs only busybox-level tools plus socat.
set -eu
binary=$1
"$binary" &
pid=$!
sleep 0.5
test "$(echo hello | socat - TCP:127.0.0.1:7)" = hello
test "$(echo hello | socat -t1 - UDP:127.0.0.1:7)" = hello
kill -TERM "$pid"
wait "$pid"
echo "smoke test passed"
