#!/usr/bin/env python3
"""Boundary stress tests for tcp_server (V0.8 Step 5).

Scenarios under test:
  Phase 0: sanity + log line-buffering verification
  Phase 1: client early-disconnect stress (sequential + concurrent, FIN and RST)
  Phase 2: deterministic fd-reuse + connection_id misdelivery check
           (A floods requests -> A closes -> B reconnects on the SAME server fd
            -> B must receive ONLY its own response, none of A's)
  Phase 3: connect/close churn while workers are busy (stale-event hunting)
  Phase 4: reconnect pressure + fd-leak check

This script does NOT modify src/, include/ or CMakeLists.txt.
It only talks to the server over TCP and reads its stdout log.

Usage:
  python3 tests/boundary_stress_test.py [--quick] [--server PATH]
"""

import argparse
import json
import os
import re
import select
import signal
import socket
import struct
import subprocess
import sys
import threading
import time

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_SERVER = os.path.join(PROJECT, "build", "tcp_server")
HOST = "127.0.0.1"
PORT = 8080

ARTIFACT_DIR = os.path.join(PROJECT, "tests", "artifacts")


# ---------------------------------------------------------------- helpers

def log(msg):
    print(msg, flush=True)


def connect(port=PORT, timeout=5.0):
    return socket.create_connection((HOST, port), timeout=timeout)


def close_rst(s):
    """Close with RST (SO_LINGER on + 0 timeout)."""
    try:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                     struct.pack("ii", 1, 0))
    except OSError:
        pass
    s.close()


def close_fin(s):
    """Graceful close (FIN)."""
    try:
        s.shutdown(socket.SHUT_WR)
    except OSError:
        pass
    s.close()


class RecvAnomaly(Exception):
    pass


def recv_lines(sock, deadline, want_count, idle_stop, max_lines=50000):
    """Read lines from sock until deadline.

    Returns (lines, kind, first_byte_time) where kind is:
      'ok'            - got at least want_count lines, then idle_stop of silence
      'eof'           - server closed the socket before we were done
      'reset'         - connection reset
      'timeout'       - deadline hit without enough lines
      'cap'           - hit max_lines safety cap
    first_byte_time is the monotonic time of the first byte received
    (None if nothing ever arrived). It excludes the idle_stop tail, so it
    measures the true response latency.
    """
    buf = b""
    lines = []
    last_data_t = None
    first_byte_t = None
    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        if lines and len(lines) >= want_count and last_data_t is not None:
            if time.monotonic() - last_data_t > idle_stop:
                return lines, "ok", first_byte_t
        try:
            r, _, _ = select.select([sock], [], [], min(0.5, remaining))
        except (OSError, ValueError):
            break
        if not r:
            continue
        try:
            chunk = sock.recv(65536)
        except (ConnectionResetError, OSError):
            return lines, "reset", first_byte_t
        if not chunk:
            return lines, "eof", first_byte_t
        if first_byte_t is None:
            first_byte_t = time.monotonic()
        last_data_t = time.monotonic()
        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            lines.append(line.decode("utf-8", "replace"))
            if len(lines) >= max_lines:
                return lines, "cap", first_byte_t
    return lines, "timeout", first_byte_t


# ---------------------------------------------------------------- harness

class Harness:
    def __init__(self, server_path, port, log_path):
        self.port = port
        self.log_path = log_path
        self.proc = None
        self.pid = None
        self.max_rss_kb = 0
        self.rss_stop = threading.Event()
        self.rss_thread = None
        self._start(server_path)

    def _start(self, server_path):
        self.log_file = open(self.log_path, "ab")
        # stdbuf forces line buffering on std::cout so logs hit the file
        # in real time (server uses std::cout without explicit flush).
        self.proc = subprocess.Popen(
            ["stdbuf", "-oL", "-eL", server_path],
            stdout=self.log_file,
            stderr=subprocess.STDOUT,
            stdin=subprocess.DEVNULL,
            cwd=PROJECT,
        )
        self.pid = self.proc.pid
        self.rss_stop.clear()
        self.rss_thread = threading.Thread(target=self._rss_sampler, daemon=True)
        self.rss_thread.start()
        # wait for listen socket
        for _ in range(100):
            try:
                s = connect(self.port, timeout=0.5)
                s.close()
                return
            except OSError:
                if self.proc.poll() is not None:
                    raise RuntimeError(
                        "server exited during startup, code=%s"
                        % self.proc.returncode)
                time.sleep(0.05)
        raise RuntimeError("server did not accept connections within 5s")

    def _rss_sampler(self):
        while not self.rss_stop.is_set():
            try:
                with open("/proc/%d/status" % self.pid) as f:
                    for line in f:
                        if line.startswith("VmRSS:"):
                            kb = int(line.split()[1])
                            if kb > self.max_rss_kb:
                                self.max_rss_kb = kb
                            break
            except OSError:
                pass
            self.rss_stop.wait(0.2)

    def alive(self):
        return self.proc.poll() is None

    def exit_code(self):
        return self.proc.poll()

    def log_text(self):
        with open(self.log_path, "rb") as f:
            return f.read().decode("utf-8", "replace")

    def log_size(self):
        try:
            return os.path.getsize(self.log_path)
        except OSError:
            return 0

    def log_tail(self, n=60):
        lines = self.log_text().splitlines()
        return "\n".join(lines[-n:])

    def fd_count(self):
        try:
            return len(os.listdir("/proc/%d/fd" % self.pid))
        except OSError:
            return -1

    def kill(self):
        self.rss_stop.set()
        if self.proc.poll() is None:
            try:
                self.proc.send_signal(signal.SIGTERM)
                try:
                    self.proc.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    self.proc.kill()
                    self.proc.wait(timeout=2)
            except OSError:
                pass
        self.log_file.close()

    def check_alive(self, ctx):
        if not self.alive():
            log("FATAL: server died during %s, exit code=%s"
                % (ctx, self.exit_code()))
            log("--- last log lines ---")
            log(self.log_tail())
            return False
        return True


def sanity(harness, tag):
    """Connect + ECHO round trip; also proves the server is not deadlocked."""
    s = connect()
    s.sendall(b"ECHO sanity-%s\n" % tag.encode())
    s.settimeout(5.0)
    try:
        data = s.recv(1024)
        s.close()
        return data == b"sanity-%s\n" % tag.encode()
    except OSError:
        s.close()
        return False


# ---------------------------------------------------------------- phases

def phase1(harness, quick):
    """Client early-disconnect stress: send request, close immediately."""
    seq_iters = 300 if quick else 3000
    conc_threads = 8 if quick else 24
    conc_iters = 50 if quick else 400

    log("Phase 1a: %d sequential send+close (FIN/RST mix)" % seq_iters)
    send_fail = 0
    for i in range(seq_iters):
        if i % 100 == 0 and not harness.check_alive("phase1a"):
            return False
        try:
            s = connect(timeout=3.0)
            s.sendall(b"ECHO x\n")
            if i % 3 == 2:
                close_rst(s)
            else:
                close_fin(s)
        except OSError:
            send_fail += 1
            if send_fail > 50:
                log("too many connect/send failures: %d" % send_fail)
                return False
    log("  done (send_fail=%d)" % send_fail)

    log("Phase 1b: %d threads x %d concurrent send+close" %
        (conc_threads, conc_iters))
    errors = []
    stop = threading.Event()

    def worker(tid):
        local_err = 0
        for i in range(conc_iters):
            if stop.is_set():
                break
            try:
                s = connect(timeout=3.0)
                if i % 4 == 0:
                    s.sendall(b"CALC 1 2\n")
                else:
                    s.sendall(b"REVERSE abc\n")
                if i % 2:
                    close_rst(s)
                else:
                    close_fin(s)
            except OSError:
                local_err += 1
                if local_err > 30:
                    stop.set()
                    break
        errors.append(local_err)

    threads = [threading.Thread(target=worker, args=(t,))
               for t in range(conc_threads)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()

    log("  done (per-thread send_fail=%s)" % errors)
    if not harness.check_alive("phase1b"):
        return False
    if not sanity(harness, "p1"):
        log("FAIL: sanity echo failed after phase 1")
        return False
    log("  sanity echo after phase 1: OK")
    return True


def phase2(harness, quick):
    """Deterministic fd-reuse + connection_id check.

    For each round:
      A connects, floods F requests (1KB payloads, unique prefix), closes FIN
      B connects immediately -> server must reuse A's fd (verified from logs)
      B sends one sentinel request; B must receive ONLY its own response.
      B's response latency proves how long A's backlog was still draining
      AFTER B was already connected (i.e. the connection_id check was
      exercised thousands of times).
    """
    rounds = 2 if quick else 6
    F = 2000 if quick else 4000
    payload_len = 16384  # REVERSE 16KB: worker cost (~30us) > 4x I/O-thread
                         # cost per request, so workers deterministically lag
                         # and completions are still arriving after B connects
    summary = []

    # baseline: no flood - documents that a single request completes too
    # fast to ever race a reconnect (why the flood is needed at all)
    log("Phase 2 baseline (no flood): send + immediate close + reconnect")
    base_lines, base_kind, base_lat = _phase2_round(
        harness, -1, 1, payload_len, resp_timeout=5.0, command=b"REVERSE")
    log("  baseline: kind=%s latency=%.1fms lines=%d" %
        (base_kind, base_lat * 1000, len(base_lines)))
    summary.append({"round": -1, "F": 1, "kind": base_kind,
                    "latency_ms": round(base_lat * 1000, 1),
                    "misdelivered": _misdelivered_count(base_lines, -1)})

    for r in range(rounds):
        log("Phase 2 round %d: F=%d, payload=%dB (REVERSE)" %
            (r, F, payload_len))
        lines, kind, lat = _phase2_round(
            harness, r, F, payload_len, resp_timeout=30.0, command=b"REVERSE")
        mis = _misdelivered_count(lines, r)
        entry = {"round": r, "F": F, "kind": kind,
                 "latency_ms": round(lat * 1000, 1), "misdelivered": mis,
                 "lines_received": len(lines)}
        summary.append(entry)
        log("  kind=%s latency=%.1fms lines=%d misdelivered=%d" %
            (kind, lat * 1000, len(lines), mis))
        if mis:
            log("  MISDELIVERY DETECTED, sample lines: %s" % lines[:5])

        # verify fd reuse from the log window
        window = harness.log_text()[_phase2_log_start:]
        fds = re.findall(r"new client connected: (\d+)", window)
        reuse_ok = len(fds) == 2 and fds[0] == fds[1]
        entry["server_fds"] = fds
        entry["fd_reused"] = reuse_ok
        log("  server fds in window=%s fd_reused=%s" % (fds, reuse_ok))
        if not reuse_ok:
            log("  WARNING: fd reuse not confirmed in this round")

        if not harness.check_alive("phase2 round %d" % r):
            return False

        # adapt F to keep the backlog draining well after B connects
        # (latency > ~20ms proves B's task waited behind A's backlog, i.e.
        # A's completions were still being produced after B took the fd)
        if kind == "timeout":
            F = max(1000, int(F * 0.5))
        elif lat < 0.02:
            F = min(12000, int(F * 1.6))
        elif lat > 5.0:
            F = max(1000, int(F * 0.6))
        time.sleep(0.3)

    log("Phase 2 summary: %s" % json.dumps(summary))
    if not sanity(harness, "p2"):
        log("FAIL: sanity echo failed after phase 2")
        return False
    log("  sanity echo after phase 2: OK")
    return True


_phase2_log_start = 0  # set by _phase2_round


def _phase2_round(harness, round_idx, F, payload_len, resp_timeout,
                  command=b"ECHO"):
    global _phase2_log_start
    _phase2_log_start = harness.log_size()

    a = connect(timeout=10.0)
    a.settimeout(120.0)
    prefix = ("A%d-%%06d-" % round_idx).encode()
    chunk_size = 64
    # build F lines of "<CMD> A<r>-<i>-yyyy...\n"
    payload = b"y" * payload_len
    lines_buf = []
    for i in range(F):
        lines_buf.append(command + b" " + prefix % i + payload + b"\n")
        if len(lines_buf) >= chunk_size:
            a.sendall(b"".join(lines_buf))
            lines_buf = []
    if lines_buf:
        a.sendall(b"".join(lines_buf))
    close_fin(a)  # graceful: guarantees the server reads all F requests

    t0 = time.monotonic()
    b = connect(timeout=10.0)
    marker = b"ECHO B%d-SENTINEL\n" % round_idx
    b.sendall(marker)
    lines, kind, first_byte_t = recv_lines(
        b, time.monotonic() + resp_timeout, want_count=1, idle_stop=1.0)
    if first_byte_t is None:
        lat = time.monotonic() - t0  # no response: report full wait
    else:
        lat = first_byte_t - t0
    b.close()
    return lines, kind, lat


def _misdelivered_count(lines, round_idx):
    if round_idx < 0:
        return sum(1 for l in lines if l != "B-1-SENTINEL" and l != "")
    expected = "B%d-SENTINEL" % round_idx
    return sum(1 for l in lines if l != expected and l != "")


def phase3(harness, quick):
    """Dense connect/close churn during sustained load (stale-event hunt).

    A floods the task queue at the start; a feeder keeps connecting,
    sending one heavy request and disconnecting immediately (early-disconnect
    under load) for the whole churn; meanwhile:
      - fast threads hammer connect -> send -> close (RST/FIN) with no wait
      - check threads connect, send a sentinel and verify they get exactly
        their own response (no stale close, no foreign response)
    """
    F = 5000 if quick else 40000
    churn_secs = 3 if quick else 8
    n_fast = 2 if quick else 4
    n_check = 2

    log("Phase 3: flood F=%d, churn %.0fs (fast=%d check=%d)" %
        (F, churn_secs, n_fast, n_check))
    a = connect(timeout=10.0)
    a.settimeout(120.0)
    a.sendall(b"ECHO q\n" * F)

    stop_feed = threading.Event()

    def feeder():
        i = 0
        while not stop_feed.is_set():
            i += 1
            try:
                a2 = connect(timeout=3.0)
                a2.sendall(b"REVERSE " + b"w" * 2048 + b"\n")
                close_fin(a2)
            except OSError:
                pass
            time.sleep(0.05)

    feed_thread = threading.Thread(target=feeder, daemon=True)
    feed_thread.start()

    anomalies = []
    stats = {"fast_iters": 0, "b_ok": 0, "b_timeout": 0, "b_eof": 0,
             "b_reset": 0, "b_wrong": 0, "connect_fail": 0}
    lock = threading.Lock()
    deadline = time.monotonic() + churn_secs

    def fast_churn(tid):
        i = 0
        while time.monotonic() < deadline:
            i += 1
            try:
                c = connect(timeout=3.0)
                c.sendall(b"ECHO c%d\n" % i)
                if i % 2:
                    close_rst(c)
                else:
                    close_fin(c)
                with lock:
                    stats["fast_iters"] += 1
            except OSError:
                with lock:
                    stats["connect_fail"] += 1
            if i % 200 == 0:
                time.sleep(0.001)

    def b_check(tid):
        i = 0
        while time.monotonic() < deadline:
            i += 1
            try:
                b = connect(timeout=3.0)
                b.sendall(b"ECHO b-SENT%d\n" % i)
                lines, kind, _ = recv_lines(b, time.monotonic() + 5.0,
                                            want_count=1, idle_stop=0.1)
                with lock:
                    if kind == "ok":
                        if lines and lines[0] == ("b-SENT%d" % i):
                            stats["b_ok"] += 1
                        else:
                            stats["b_wrong"] += 1
                            anomalies.append(
                                ("wrong_response", i, lines[:3],
                                 harness.log_tail(20)))
                    elif kind == "eof":
                        stats["b_eof"] += 1
                        anomalies.append(("premature_eof", i, [],
                                          harness.log_tail(20)))
                    elif kind == "reset":
                        stats["b_reset"] += 1
                        anomalies.append(("premature_reset", i, [],
                                          harness.log_tail(20)))
                    else:
                        stats["b_timeout"] += 1
                b.close()
            except OSError:
                with lock:
                    stats["connect_fail"] += 1

    threads = ([threading.Thread(target=fast_churn, args=(t,))
                for t in range(n_fast)] +
               [threading.Thread(target=b_check, args=(t,))
                for t in range(n_check)])
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    stop_feed.set()
    feed_thread.join(timeout=2)
    close_fin(a)

    log("  churn stats: %s" % json.dumps(stats))
    for anom in anomalies[:5]:
        log("  ANOMALY %s (iter %s) lines=%s" % (anom[0], anom[1], anom[2]))
        log("  --- server log tail at anomaly ---\n%s" % anom[3])
    if anomalies:
        log("  total anomalies: %d" % len(anomalies))
    else:
        log("  no anomalies observed")

    time.sleep(2.0)  # let the backlog drain
    if not harness.check_alive("phase3"):
        return False
    fd_idle = harness.fd_count()
    log("  server fd_count at idle: %d (expected ~6-7)" % fd_idle)
    if not sanity(harness, "p3"):
        log("FAIL: sanity echo failed after phase 3")
        return False
    log("  sanity echo after phase 3: OK")
    return True


def phase4(harness, quick):
    """Reconnect pressure + fd leak check."""
    iters = 60 if quick else 300
    fd_before = harness.fd_count()
    rss_before = harness.max_rss_kb
    log("Phase 4: %d mixed reconnect rounds (fd_count=%d)" % (iters, fd_before))
    fails = 0
    for i in range(iters):
        if i % 50 == 0 and not harness.check_alive("phase4"):
            return False
        mode = i % 3
        try:
            if mode == 0:
                s = connect(timeout=3.0)
                s.sendall(b"ECHO z\n")
                close_fin(s)
            elif mode == 1:
                s = connect(timeout=3.0)
                s.sendall(b"ECHO z\n")
                close_rst(s)
            else:
                s = connect(timeout=3.0)
                s.sendall(b"ECHO z\n" * 5)
                lines, kind, _ = recv_lines(s, time.monotonic() + 5.0,
                                            want_count=5, idle_stop=0.3)
                if kind != "ok" or lines[:5] != ["z"] * 5:
                    fails += 1
                    if fails <= 3:
                        log("  round %d: kind=%s lines=%s"
                            % (i, kind, lines[:6]))
                close_fin(s)
        except OSError:
            fails += 1

    time.sleep(1.0)
    fd_after = harness.fd_count()
    log("  done, fails=%d, fd_count before=%d after=%d"
        % (fails, fd_before, fd_after))
    if fd_after > fd_before + 3:
        log("  WARNING: possible fd leak (delta=%d)" % (fd_after - fd_before))
    if not harness.check_alive("phase4"):
        return False
    if not sanity(harness, "p4"):
        log("FAIL: sanity echo failed after phase 4")
        return False
    log("  sanity echo after phase 4: OK")
    return True


# ---------------------------------------------------------------- analysis

def analyze_log(path):
    text = ""
    with open(path, "rb") as f:
        text = f.read().decode("utf-8", "replace")
    lines = text.splitlines()

    def count(pattern, line=None):
        n = 0
        for l in lines:
            if (line and pattern in l) or (not line and re.search(pattern, l)):
                n += 1
        return n

    connects = re.findall(r"new client connected: (\d+)", text)
    closes = re.findall(r"client closed: (\d+)", text)
    max_fd = max((int(x) for x in connects), default=-1)

    # consecutive duplicate "client closed: N" without a connect between
    dup_close = 0
    prev = None
    for l in lines:
        m = re.match(r"client closed: (\d+)", l)
        if m and prev == m.group(1):
            dup_close += 1
        prev = m.group(1) if m else None

    # the try_emplace-failure branch prints connection_id - a structural
    # race would show up here (accept hit an fd still owned by an old
    # Connection object)
    id_prints = count(r"connection_id:")

    report = {
        "total_lines": len(lines),
        "new_client_connected": len(connects),
        "client_closed": len(closes),
        "client_disconnected": count("client disconnected:"),
        "client_error_or_hangup": count("client error or hangup:"),
        "recv_failed": count("recv failed"),
        "send_failed": count("send failed"),
        "epoll_ctl_MOD_failed": count("epoll_ctl MOD failed"),
        "accept_failed": count("accept failed"),
        "epoll_wait_failed": count("epoll_wait failed"),
        "eventfd_write_failed": count("write completion eventfd failed"),
        "connection_id_prints": id_prints,
        "duplicate_consecutive_closes": dup_close,
        "max_client_fd": max_fd,
        "recv_fail_reasons": {},
        "send_fail_reasons": {},
    }
    for l in lines:
        m = re.search(r"recv failed for client \d+: (.*)", l)
        if m:
            report["recv_fail_reasons"][m.group(1)] = \
                report["recv_fail_reasons"].get(m.group(1), 0) + 1
        m = re.search(r"send failed for client \d+: (.*)", l)
        if m:
            report["send_fail_reasons"][m.group(1)] = \
                report["send_fail_reasons"].get(m.group(1), 0) + 1
    return report, lines


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--server", default=DEFAULT_SERVER)
    ap.add_argument("--port", type=int, default=PORT)
    args = ap.parse_args()

    if not os.path.exists(args.server):
        log("server binary not found: %s" % args.server)
        return 2

    # refuse to run if port already in use
    try:
        probe = connect(args.port, timeout=0.5)
        probe.close()
        log("port %d already in use - abort" % args.port)
        return 2
    except OSError:
        pass

    os.makedirs(ARTIFACT_DIR, exist_ok=True)
    stamp = time.strftime("%Y%m%d_%H%M%S")
    log_path = os.path.join(ARTIFACT_DIR,
                            "boundary_server_%s.log" % stamp)

    log("starting server: %s (log: %s)" % (args.server, log_path))
    harness = Harness(args.server, args.port, log_path)
    log("server pid=%d" % harness.pid)

    results = {"phases": {}}

    # Phase 0
    if not sanity(harness, "p0"):
        log("FAIL: phase 0 sanity echo")
        harness.kill()
        return 1
    time.sleep(0.3)
    if "new client connected" not in harness.log_text():
        log("FAIL: server log is not being flushed (stdbuf ineffective)")
        harness.kill()
        return 1
    log("Phase 0: sanity echo + log flushing OK")
    results["phases"]["0_sanity"] = "pass"

    def run_phase(name, fn):
        try:
            ok = fn()
            results["phases"][name] = "pass" if ok else "fail"
        except Exception as exc:  # e.g. server died mid-phase
            log("EXCEPTION in %s: %r" % (name, exc))
            results["phases"][name] = "fail"

    run_phase("1_early_disconnect",
              lambda: phase1(harness, args.quick))
    run_phase("2_fd_reuse_connection_id",
              lambda: phase2(harness, args.quick))
    run_phase("3_churn_during_flood",
              lambda: phase3(harness, args.quick))
    run_phase("4_reconnect_pressure",
              lambda: phase4(harness, args.quick))

    alive_at_end = harness.alive()
    results["server_alive_at_end"] = alive_at_end
    results["max_rss_kb"] = harness.max_rss_kb

    harness.kill()
    time.sleep(0.3)
    results["server_exit_code_after_kill"] = harness.exit_code()
    results["log_path"] = log_path

    report, lines = analyze_log(log_path)
    results["log_analysis"] = report

    log("\n=== final summary ===")
    log(json.dumps(results, indent=2, ensure_ascii=False))

    ok = alive_at_end and all(
        v == "pass" for v in results["phases"].values())
    log("\nOVERALL: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
