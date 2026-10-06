#!/usr/bin/env python3
"""Mutation control for the fd-reuse / connection_id test.

Runs the SAME flood+reconnect scenario used by phase 2 of
boundary_stress_test.py against an arbitrary server binary:

  A connects, floods F heavy requests (REVERSE 16KB), closes. B reconnects
  on the same server fd and sends a sentinel. B must receive ONLY its own
  response.

Expected results:
  - original V0.8 binary (has the connection_id check): misdelivered == 0
  - mutant binary without the check: misdelivered > 0 (the test must be
    able to DETECT fd-reuse misdelivery, otherwise it proves nothing)

Usage:
  python3 tests/mutation_control.py --server /path/to/tcp_server
"""

import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import boundary_stress_test as bst  # noqa: E402

PORT = 8080
PAYLOAD_LEN = 16384
COMMAND = b"REVERSE"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", required=True)
    ap.add_argument("--rounds", type=int, default=2)
    ap.add_argument("--flood", type=int, default=8000)
    args = ap.parse_args()

    os.makedirs(bst.ARTIFACT_DIR, exist_ok=True)
    stamp = time.strftime("%Y%m%d_%H%M%S")
    log_path = os.path.join(bst.ARTIFACT_DIR,
                            "mutation_control_%s.log" % stamp)

    print("starting server: %s" % args.server, flush=True)
    harness = bst.Harness(args.server, PORT, log_path)

    # Flush the fd holes left by the Harness startup probe: round-trip one
    # echo and wait until the server has processed every close, so the
    # next connection deterministically gets the lowest free fd and A/B
    # reuse is guaranteed.
    s = bst.connect()
    s.sendall(b"ECHO flush\n")
    try:
        s.recv(1024)
    except OSError:
        pass
    bst.close_fin(s)
    time.sleep(0.5)

    results = []
    for r in range(args.rounds):
        lines, kind, lat = bst._phase2_round(
            harness, r, args.flood, PAYLOAD_LEN,
            resp_timeout=60.0, command=COMMAND)
        mis = bst._misdelivered_count(lines, r)
        window = harness.log_text()[bst._phase2_log_start:]
        fds = re.findall(r"new client connected: (\d+)", window)
        reuse = len(fds) == 2 and fds[0] == fds[1]
        print("round %d: kind=%s latency=%.1fms lines=%d misdelivered=%d "
              "fds=%s fd_reused=%s"
              % (r, kind, lat * 1000, len(lines), mis, fds, reuse),
              flush=True)
        if mis:
            print("  first misdelivered lines (truncated): %s"
                  % [l[:60] for l in lines[:3]], flush=True)
        results.append((mis, reuse))
        if not harness.alive():
            print("server DIED during round %d, exit=%s"
                  % (r, harness.exit_code()), flush=True)
            break
        time.sleep(1.0)

    alive = harness.alive()
    harness.kill()
    print("server_alive_at_end=%s" % alive, flush=True)
    print("total_misdelivered=%d" % sum(m for m, _ in results), flush=True)
    print("all_rounds_fd_reused=%s" % all(x for _, x in results), flush=True)
    print("log: %s" % log_path, flush=True)

    if not alive:
        return 2
    if not all(x for _, x in results):
        print("WARNING: fd reuse not confirmed in every round - "
              "misdelivery numbers may be understated", flush=True)
    if sum(m for m, _ in results) > 0:
        print("MUTATION DETECTED: test correctly caught misdelivery",
              flush=True)
        return 0
    print("no misdelivery observed", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
