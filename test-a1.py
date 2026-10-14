#!/usr/bin/env python3

#
# Boot xv6 with a fresh file system, run tests, and report.
#
# ./test-a1.py            (runs every a1test group)
# ./test-a1.py find       (runs one a1test group)
# ./test-a1.py trace      (checks the kernel's trace lines on the console)
# ./test-a1.py usertests  (runs usertests -q)
#
# The full console output is saved to a1test.out. Exits 0 on success.

import os, re, signal, subprocess, sys, time

target = sys.argv[1] if len(sys.argv) > 1 else ""

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

def wait_for(regex, timeout, crash=r"^panic", start=0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        text = output()[start:]
        if re.search(regex, text, re.M):
            return text
        if re.search(crash, text, re.M):
            return None
        time.sleep(0.2)
    return None

def stop():
    try:
        send("\x01x")
        proc.wait(timeout=10)
    except Exception:
        os.killpg(proc.pid, signal.SIGKILL)

# Run one command in the xv6 shell and return the console lines it
# produced (without the echoed command line), or None on timeout.
def shell(cmd, n, timeout=60):
    mark = "==END%d" % n
    start = len(output())
    send("%s; echo %s\n" % (cmd, mark))
    text = wait_for(r"^%s$" % mark, timeout, start=start)
    if text is None:
        return None
    body = text[:re.search(r"^%s$" % mark, text, re.M).start()]
    return [l for l in body.splitlines() if not l.startswith("$ ")]

# Single-command modes: a1test groups and usertests.
def run_summary(cmd, done, good, crash, timeout):
    start = len(output())
    send(cmd + "\n")
    text = wait_for(done, timeout, crash=crash, start=start)
    if text is None:
        print(output()[start:])
        print("FAIL: no summary line (timeout or crash)")
        return 1
    m = re.search(done + r".*$", text, re.M)
    print(text[:m.end()])
    return 0 if re.search(good, text, re.M) else 1

# ---- trace: the kernel's lines are checked here, since they go to the
# console rather than to the traced process's fds.

TL = re.compile(r"^\[(\d+)\] (\w+)\((.*)\) -> (-?\d+)$")

def tlines(lines):
    return [m.groups() for m in map(TL.match, lines) if m]

def t_spec(lines):
    # trace 32800 grep hello README: open and read only.
    t = tlines(lines)
    if not t:
        return ["no trace lines"]
    errs = []
    pid = t[0][0]
    if t[0][1:] != ("open", '"README"', "3"):
        errs.append("first line should be open(\"README\") -> 3: %s" % (t[0],))
    reads = t[1:]
    if not reads or reads[-1][1:] != ("read", "3", "0"):
        errs.append("last line should be read(3) -> 0")
    for x in reads:
        if x[1] != "read" or x[2] != "3" or int(x[3]) < 0:
            errs.append("unexpected line %s" % (x,))
    for x in reads[:-1]:
        if int(x[3]) <= 0:
            errs.append("only the last read should return 0: %s" % (x,))
    if any(x[0] != pid for x in t):
        errs.append("pids differ")
    return errs

def t_exact(want):
    def check(lines):
        got = [x[1:] for x in tlines(lines)]
        return [] if got == want else ["want %s, got %s" % (want, got)]
    return check

def t_contains(text):
    def check(lines):
        return [] if text in lines else ["missing output line %r" % text]
    return check

def t_help(lines):
    # trace <mask> a1test tracehelp: every argument style, fork
    # inheritance (the child's line has the child's pid), and no exit.
    t = tlines(lines)
    if not t:
        return ["no trace lines"]
    P = t[0][0]
    forks = [x for x in t if x[0] == P and x[1] == "fork"]
    if len(forks) != 1:
        return ["want one fork line, got %s" % t]
    C = forks[0][3]
    want_parent = [
        ("exec", '"a1test"', "2"),
        ("getpid", "", P),
        ("fork", "", C),
        ("wait", "0", C),
        ("open", "?", "-1"),
        ("exec", "?", "-1"),
        ("close", "-1", "-1"),
        ("read", "99", "-1"),
        ("open", '"README"', "3"),
        ("close", "3", "0"),
    ]
    errs = []
    got_parent = [x[1:] for x in t if x[0] == P]
    got_child = [x[1:] for x in t if x[0] == C]
    if C == P:
        errs.append("child pid equals parent pid")
    if got_parent != want_parent:
        errs.append("parent lines:\n    want %s\n    got  %s" % (want_parent, got_parent))
    if got_child != [("getpid", "", C)]:
        errs.append("child lines: want getpid() -> %s, got %s" % (C, got_child))
    if len(t) != len(want_parent) + 1:
        errs.append("extra lines: %s" % t)
    return errs

# mask bits: fork 1, exit 2, wait 3, read 5, exec 7, getpid 11,
# open 15, close 21, trace 23
HELPMASK = sum(1 << i for i in (1, 2, 3, 5, 7, 11, 15, 21))

TRACE_CASES = [
    ("spec: trace 32800 grep hello README", "trace 32800 grep hello README", t_spec),
    ("exec path printed after successful exec", "trace 128 echo hi",
     t_exact([("exec", '"echo"', "2")])),
    ("traced command still runs", "trace 128 echo hi", t_contains("hi")),
    ("trace traces itself", "trace 8388608 echo hi",
     t_exact([("trace", "8388608", "0")])),
    ("exit is never printed", "trace 2 echo hi", t_exact([])),
    ("arg styles, fork inheritance, bad pointers",
     "trace %d a1test tracehelp" % HELPMASK, t_help),
    ("untraced afterwards: freed masks are cleared", "a1test tracehelp",
     t_exact([])),
    ("untraced echo", "echo hi", t_exact([])),
]

def run_trace():
    failed = 0
    for n, (name, cmd, check) in enumerate(TRACE_CASES):
        lines = shell(cmd, n)
        errs = ["timeout"] if lines is None else check(lines)
        if errs:
            failed += 1
            print("FAIL %s" % name)
            for e in errs:
                print("  " + e)
            for l in lines or []:
                print("  | " + l)
        else:
            print("PASS %s" % name)
    if failed:
        print("TRACE: %d FAILED, %d passed" % (failed, len(TRACE_CASES) - failed))
        return 1
    print("TRACE: ALL PASSED (%d)" % len(TRACE_CASES))
    return 0

result = 1
try:
    if wait_for(r"init: starting sh", 60) is None:
        print("FAIL: xv6 did not boot")
    elif target == "trace":
        result = run_trace()
    elif target == "usertests":
        # usertests faults on purpose, so only a panic counts as a crash.
        result = run_summary("usertests -q",
                             r"^(ALL TESTS PASSED|SOME TESTS FAILED)",
                             r"^ALL TESTS PASSED", r"^panic", 900)
    else:
        result = run_summary(("a1test " + target).strip(), r"^A1TEST: ",
                             r"^A1TEST: ALL PASSED",
                             r"^panic|usertrap\(\): unexpected", 300)
finally:
    stop()
    with open("a1test.out", "w") as f:
        f.write(output())

sys.exit(result)
