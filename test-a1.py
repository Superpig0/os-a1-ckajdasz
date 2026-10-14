#!/usr/bin/env python3

#
# Boot xv6 with a fresh file system, run one command, and report.
#
# ./test-a1.py            (runs every a1test group)
# ./test-a1.py find       (runs one a1test group: sleep, find or xargs)
# ./test-a1.py usertests  (runs usertests -q)
#
# The full console output is saved to a1test.out. Exits 0 on success.

import os, re, signal, subprocess, sys, time

target = sys.argv[1] if len(sys.argv) > 1 else ""
if target == "usertests":
    cmd = "usertests -q"
    done = r"^(ALL TESTS PASSED|SOME TESTS FAILED)"
    good = r"^ALL TESTS PASSED"
    timeout = 900
else:
    cmd = ("a1test " + target).strip()
    done = r"^A1TEST: "
    good = r"^A1TEST: ALL PASSED"
    timeout = 300

# A fresh fs.img each run, so earlier runs leave nothing behind.
subprocess.run(["rm", "-f", "fs.img"], check=True)
if subprocess.run(["make", "-s", "kernel/kernel", "fs.img"]).returncode != 0:
    print("FAIL: build failed")
    sys.exit(1)

proc = subprocess.Popen(["make", "-s", "qemu"], stdin=subprocess.PIPE,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                        start_new_session=True)
os.set_blocking(proc.stdout.fileno(), False)
outbytes = bytearray()

def output():
    while True:
        try:
            buf = os.read(proc.stdout.fileno(), 4096)
        except BlockingIOError:
            break
        if not buf:
            break
        outbytes.extend(buf)
    return outbytes.decode("utf-8", "replace")

def send(s):
    proc.stdin.write(s.encode())
    proc.stdin.flush()

def wait_for(regex, deadline):
    while time.time() < deadline:
        text = output()
        if re.search(regex, text, re.M):
            return text
        if re.search(r"^panic", text, re.M):
            return None
        time.sleep(0.5)
    return None

def stop():
    try:
        send("\x01x")
        proc.wait(timeout=10)
    except Exception:
        os.killpg(proc.pid, signal.SIGKILL)

result = 1
try:
    if wait_for(r"init: starting sh", time.time() + 60) is None:
        print("FAIL: xv6 did not boot")
    else:
        start = len(output())
        send(cmd + "\n")
        text = wait_for(done, time.time() + timeout)
        if text is None:
            print(output()[start:])
            print("FAIL: no summary line (timeout or panic)")
        else:
            body = text[start:]
            m = re.search(done + r".*$", body, re.M)
            print(body[:m.end()] if m else body)
            result = 0 if re.search(good, body, re.M) else 1
finally:
    stop()
    with open("a1test.out", "w") as f:
        f.write(output())

sys.exit(result)
