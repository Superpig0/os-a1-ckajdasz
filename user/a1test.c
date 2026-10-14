// a1test: tests for the Part 1 user programs (sleep, find, xargs).
//
//   a1test            run every group
//   a1test <group>    run one group
//   a1test argc ...   helper: print "argc=N" (N counts argv[0])
//   a1test fail ...   helper: exit 1 if any argument is "bad",
//                     otherwise print the arguments and exit 0
//
// Each case runs a program with stdin, stdout and stderr redirected to
// files under /a1tmp (files, not pipes, so a chatty child can never
// deadlock against a full 512-byte pipe), then compares the output and
// the exit status exactly.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "kernel/fs.h"
#include "kernel/param.h"
#include "user/user.h"

#define ARGV(...) ((char *[]){__VA_ARGS__, 0})
#define OUTMAX 4096
#define MAXLINES 256
#define TMPDIR "/a1tmp"
#define TIN TMPDIR "/in"
#define TOUT TMPDIR "/out"
#define TERR TMPDIR "/err"

char out[OUTMAX], err[OUTMAX];
int npass, nfail;

int
writefile(char *path, char *s)
{
  int fd;

  if ((fd = open(path, O_CREATE | O_TRUNC | O_WRONLY)) < 0)
    return -1;
  if (s && write(fd, s, strlen(s)) != strlen(s)) {
    close(fd);
    return -1;
  }
  close(fd);
  return 0;
}

void
readfile(char *path, char *buf)
{
  int fd, n, tot;

  tot = 0;
  if ((fd = open(path, O_RDONLY)) >= 0) {
    while (tot < OUTMAX - 1 && (n = read(fd, buf + tot, OUTMAX - 1 - tot)) > 0)
      tot += n;
    close(fd);
  }
  buf[tot] = '\0';
}

// Run argv with the given stdin text, leaving its stdout and stderr in
// out and err. Returns the exit status.
int
run(char **argv, char *in)
{
  int pid, status;

  if (writefile(TIN, in) < 0) {
    fprintf(2, "a1test: cannot write %s\n", TIN);
    exit(1);
  }
  pid = fork();
  if (pid < 0) {
    fprintf(2, "a1test: fork failed\n");
    exit(1);
  }
  if (pid == 0) {
    close(0);
    open(TIN, O_RDONLY);
    close(1);
    open(TOUT, O_CREATE | O_TRUNC | O_WRONLY);
    close(2);
    open(TERR, O_CREATE | O_TRUNC | O_WRONLY);
    exec(argv[0], argv);
    fprintf(2, "a1test: exec %s failed\n", argv[0]);
    exit(127);
  }
  if (wait(&status) != pid)
    status = -1;
  readfile(TOUT, out);
  readfile(TERR, err);
  return status;
}

// Compare two lines, each ended by '\n' or '\0'.
int
linecmp(char *a, char *b)
{
  int ca, cb;

  while (*a && *a != '\n' && *a == *b) {
    a++;
    b++;
  }
  ca = *a == '\n' ? 0 : (uchar)*a;
  cb = *b == '\n' ? 0 : (uchar)*b;
  return ca - cb;
}

// Sort the lines of s in place, as the graders do with find's output.
void
sortlines(char *s)
{
  static char *line[MAXLINES];
  static char tmp[OUTMAX];
  char *p, *q, *t;
  int n, i, j;

  n = 0;
  for (p = s; *p && n < MAXLINES;) {
    line[n++] = p;
    while (*p && *p != '\n')
      p++;
    if (*p)
      p++;
  }
  for (i = 1; i < n; i++) {
    t = line[i];
    for (j = i; j > 0 && linecmp(line[j - 1], t) > 0; j--)
      line[j] = line[j - 1];
    line[j] = t;
  }
  q = tmp;
  for (i = 0; i < n; i++) {
    for (p = line[i]; *p && *p != '\n';)
      *q++ = *p++;
    if (*p == '\n')
      *q++ = '\n';
  }
  *q = '\0';
  strcpy(s, tmp);
}

// Run one case. werr == 0 means stderr is not checked. If sorted is
// set, stdout lines are compared in sorted order.
void
check(char *name, char **argv, char *in, char *wout, char *werr,
      int wstatus, int sorted)
{
  static char want[OUTMAX];
  int status;

  status = run(argv, in);
  strcpy(want, wout);
  if (sorted) {
    sortlines(out);
    sortlines(want);
  }
  if (status == wstatus && strcmp(out, want) == 0 &&
      (werr == 0 || strcmp(err, werr) == 0)) {
    npass++;
    printf("PASS %s\n", name);
    return;
  }
  nfail++;
  printf("FAIL %s\n", name);
  printf("  status: want %d, got %d\n", wstatus, status);
  printf("  stdout want: [%s]\n", want);
  printf("  stdout got:  [%s]\n", out);
  if (werr)
    printf("  stderr want: [%s]\n", werr);
  printf("  stderr got:  [%s]\n", err);
}

void
ok(char *name, int cond)
{
  if (cond) {
    npass++;
    printf("PASS %s\n", name);
  } else {
    nfail++;
    printf("FAIL %s\n", name);
  }
}

// The path rmtree1() is removing. Like find, it is one global buffer
// extended in place, since a per-frame copy overflows the one-page user
// stack on the deep test tree.
char rmpath[256];

// Remove rmpath[0..len) and, if it is a directory, everything under it.
void
rmtree1(int len)
{
  struct dirent *de;
  struct stat st;
  char *p;
  int fd, n, i;

  if (stat(rmpath, &st) < 0)
    return;
  if (st.type == T_DIR && len + 1 + DIRSIZ + 1 <= sizeof(rmpath)) {
    if ((fd = open(rmpath, O_RDONLY)) < 0)
      return;
    if ((de = malloc(st.size)) == 0) {
      close(fd);
      return;
    }
    n = read(fd, de, st.size) / sizeof(*de);
    close(fd);
    p = rmpath + len + 1;
    for (i = 0; i < n; i++) {
      if (de[i].inum == 0)
        continue;
      rmpath[len] = '/';
      memmove(p, de[i].name, DIRSIZ);
      p[DIRSIZ] = '\0';
      if (strcmp(p, ".") != 0 && strcmp(p, "..") != 0)
        rmtree1(len + 1 + strlen(p));
    }
    rmpath[len] = '\0';
    free(de);
  }
  unlink(rmpath);
}

void
rmtree(char *path)
{
  strcpy(rmpath, path);
  rmtree1(strlen(path));
}

void
mkfile(char *path)
{
  if (writefile(path, "x\n") < 0)
    fprintf(2, "a1test: cannot create %s\n", path);
}

#define SLEEPUSAGE "usage: sleep <ticks>\n"

void
testsleep(void)
{
  int t0, status;

  check("sleep 0", ARGV("sleep", "0"), "", "", "", 0, 0);
  check("sleep 3", ARGV("sleep", "3"), "", "", "", 0, 0);
  check("sleep 007", ARGV("sleep", "007"), "", "", "", 0, 0);

  t0 = uptime();
  status = run(ARGV("sleep", "10"), "");
  ok("sleep 10 waits at least 10 ticks", status == 0 && uptime() - t0 >= 10);

  check("sleep: no argument", ARGV("sleep"), "", "", SLEEPUSAGE, 1, 0);
  check("sleep: two arguments", ARGV("sleep", "1", "2"), "", "", SLEEPUSAGE, 1, 0);
  check("sleep: negative", ARGV("sleep", "-1"), "", "", SLEEPUSAGE, 1, 0);
  check("sleep: plus sign", ARGV("sleep", "+5"), "", "", SLEEPUSAGE, 1, 0);
  check("sleep: letters", ARGV("sleep", "abc"), "", "", SLEEPUSAGE, 1, 0);
  check("sleep: digits then letter", ARGV("sleep", "5x"), "", "", SLEEPUSAGE, 1, 0);
  check("sleep: letter then digits", ARGV("sleep", "x5"), "", "", SLEEPUSAGE, 1, 0);
  check("sleep: empty string", ARGV("sleep", ""), "", "", SLEEPUSAGE, 1, 0);
  check("sleep: space inside", ARGV("sleep", "1 2"), "", "", SLEEPUSAGE, 1, 0);
}

#define FINDUSAGE "usage: find <dir> <pattern> [-type f|d]\n"
#define DEEP 15
#define WIDE 20

char wbuf[OUTMAX];

void
append(char *dst, char *s)
{
  strcpy(dst + strlen(dst), s);
}

// wide/sNN for i in 0..WIDE-1
void
widename(char *dst, int i)
{
  strcpy(dst, "wide/s00");
  dst[6] = '0' + i / 10;
  dst[7] = '0' + i % 10;
}

// The tree the find tests search, under /a1t:
//   a/y.c  a/b/x.c                        the spec's example
//   p/{ab,axxbyy,ba,abc.c,x.c,readme,c}   pattern targets
//   p/dd/ab                               a match one level down
//   deep/d/d/.../d/leaf                   DEEP nested directories
//   wide/s00/f ... wide/s19/f             WIDE sibling directories
//   long/abcdefghijklmn                   a DIRSIZ-long name
void
mkfindtree(void)
{
  char p[128];
  int i;

  rmtree("/a1t");
  mkdir("/a1t");
  chdir("/a1t");

  mkdir("a");
  mkdir("a/b");
  mkfile("a/b/x.c");
  mkfile("a/y.c");

  mkdir("p");
  mkfile("p/ab");
  mkfile("p/axxbyy");
  mkfile("p/ba");
  mkfile("p/abc.c");
  mkfile("p/x.c");
  mkfile("p/readme");
  mkfile("p/c");
  mkdir("p/dd");
  mkfile("p/dd/ab");

  strcpy(p, "deep");
  mkdir(p);
  for (i = 0; i < DEEP; i++) {
    append(p, "/d");
    mkdir(p);
  }
  append(p, "/leaf");
  mkfile(p);

  mkdir("wide");
  for (i = 0; i < WIDE; i++) {
    widename(p, i);
    mkdir(p);
    append(p, "/f");
    mkfile(p);
  }

  mkdir("long");
  mkfile("long/abcdefghijklmn");
}

#define FIND(...) ARGV("/find", __VA_ARGS__)
#define PALL "p/ab\np/abc.c\np/axxbyy\np/ba\np/c\np/dd\np/dd/ab\np/readme\np/x.c\n"
#define PFILES "p/ab\np/abc.c\np/axxbyy\np/ba\np/c\np/dd/ab\np/readme\np/x.c\n"

void
testfind(void)
{
  char p[128];
  int i;

  mkfindtree();

  // The spec's examples.
  check("find: spec a *.c", FIND("a", "*.c"), "", "a/y.c\na/b/x.c\n", "", 0, 1);
  check("find: spec a b -type d", FIND("a", "b", "-type", "d"), "",
        "a/b\n", "", 0, 1);

  // '*' at the end, start and middle, and combinations.
  check("find: * at end ab*", FIND("p", "ab*"), "",
        "p/ab\np/abc.c\np/dd/ab\n", "", 0, 1);
  check("find: * at start *.c", FIND("p", "*.c"), "", "p/abc.c\np/x.c\n", "", 0, 1);
  check("find: * in middle a*y", FIND("p", "a*y"), "", "p/axxbyy\n", "", 0, 1);
  check("find: a*b*", FIND("p", "a*b*"), "",
        "p/ab\np/abc.c\np/axxbyy\np/dd/ab\n", "", 0, 1);
  check("find: *b", FIND("p", "*b"), "", "p/ab\np/dd/ab\n", "", 0, 1);
  check("find: *x*", FIND("p", "*x*"), "", "p/axxbyy\np/x.c\n", "", 0, 1);
  check("find: * alone", FIND("p", "*"), "", PALL, "", 0, 1);
  check("find: **", FIND("p", "**"), "", PALL, "", 0, 1);
  check("find: a**b", FIND("p", "a**b"), "", "p/ab\np/dd/ab\n", "", 0, 1);

  // Exact names: no '*', so substrings and prefixes don't count.
  check("find: exact c", FIND("p", "c"), "", "p/c\n", "", 0, 1);
  check("find: exact ab", FIND("p", "ab"), "", "p/ab\np/dd/ab\n", "", 0, 1);
  check("find: prefix only abc", FIND("p", "abc"), "", "", "", 0, 1);
  check("find: no match", FIND("p", "zzz"), "", "", "", 0, 1);

  // -type
  check("find: -type f *", FIND("p", "*", "-type", "f"), "", PFILES, "", 0, 1);
  check("find: -type d *", FIND("p", "*", "-type", "d"), "", "p/dd\n", "", 0, 1);
  check("find: -type f dd", FIND("p", "dd", "-type", "f"), "", "", "", 0, 1);
  check("find: -type d dd", FIND("p", "dd", "-type", "d"), "", "p/dd\n", "", 0, 1);
  check("find: -type f device", FIND("/", "console", "-type", "f"), "",
        "/console\n", "", 0, 1);
  check("find: -type d device", FIND("/", "console", "-type", "d"), "",
        "", "", 0, 1);

  // Never print . or .. or <dir> itself.
  check("find: . not printed", FIND("p", "."), "", "", "", 0, 1);
  check("find: .. not printed", FIND("p", ".."), "", "", "", 0, 1);
  check("find: .* not printed", FIND("p", ".*"), "", "", "", 0, 1);
  check("find: dir itself not printed", FIND("p/dd", "dd"), "", "", "", 0, 1);

  // How <dir> is written is kept as given.
  check("find: dir .", FIND(".", "x.c"), "", "./a/b/x.c\n./p/x.c\n", "", 0, 1);
  check("find: trailing slash", FIND("p/", "c"), "", "p/c\n", "", 0, 1);
  check("find: absolute dir", FIND("/a1t/a", "*.c"), "",
        "/a1t/a/b/x.c\n/a1t/a/y.c\n", "", 0, 1);
  check("find: dir is a file", FIND("p/c", "c"), "", "", "", 0, 1);

  // Deep trees: DEEP levels of directories, more than NOFILE.
  wbuf[0] = '\0';
  strcpy(p, "deep");
  for (i = 0; i < DEEP; i++) {
    append(p, "/d");
    append(wbuf, p);
    append(wbuf, "\n");
  }
  check("find: deep -type d", FIND("deep", "d", "-type", "d"), "", wbuf, "", 0, 1);
  append(p, "/leaf\n");
  check("find: deep leaf", FIND("deep", "leaf"), "", p, "", 0, 1);

  // Many siblings, more than NOFILE: catches a leaked fd per directory.
  wbuf[0] = '\0';
  for (i = 0; i < WIDE; i++) {
    widename(p, i);
    append(wbuf, p);
    append(wbuf, "/f\n");
  }
  check("find: wide files", FIND("wide", "f"), "", wbuf, "", 0, 1);
  wbuf[0] = '\0';
  for (i = 0; i < WIDE; i++) {
    widename(p, i);
    append(wbuf, p);
    append(wbuf, "\n");
  }
  check("find: wide -type d s*", FIND("wide", "s*", "-type", "d"), "",
        wbuf, "", 0, 1);

  // A name of exactly DIRSIZ characters.
  check("find: 14-char name", FIND("long", "abcdefghijklmn"), "",
        "long/abcdefghijklmn\n", "", 0, 1);
  check("find: 14-char name *mn", FIND("long", "*mn"), "",
        "long/abcdefghijklmn\n", "", 0, 1);
  check("find: 13-char prefix", FIND("long", "abcdefghijklm"), "", "", "", 0, 1);

  // Errors.
  check("find: missing dir", FIND("nosuch", "x"), "", "",
        "find: cannot open nosuch\n", 1, 0);
  check("find: empty dir", FIND("", "x"), "", "", "find: cannot open \n", 1, 0);
  check("find: no args", ARGV("/find"), "", "", FINDUSAGE, 1, 0);
  check("find: one arg", FIND("p"), "", "", FINDUSAGE, 1, 0);
  check("find: -type without value", FIND("p", "c", "-type"), "", "",
        FINDUSAGE, 1, 0);
  check("find: -type x", FIND("p", "c", "-type", "x"), "", "", FINDUSAGE, 1, 0);
  check("find: -type ff", FIND("p", "c", "-type", "ff"), "", "", FINDUSAGE, 1, 0);
  check("find: misspelled -type", FIND("p", "c", "-typ", "f"), "", "",
        FINDUSAGE, 1, 0);
  check("find: extra arg", FIND("p", "c", "-type", "f", "x"), "", "",
        FINDUSAGE, 1, 0);

  chdir("/");
  rmtree("/a1t");
}

#define XUSAGE "usage: xargs [-n N] cmd [args...]\n"
#define XECHO(...) ARGV("xargs", __VA_ARGS__, "echo", "x")

// Build "xargs [-n N] a1test argc z z ..." with nfixed arguments after
// "xargs [-n N]", i.e. a1test, argc and nfixed-2 copies of z.
char **
xargsfixed(char *n, int nfixed)
{
  static char *av[MAXARG + 1];
  int i, k;

  k = 0;
  av[k++] = "xargs";
  if (n) {
    av[k++] = "-n";
    av[k++] = n;
  }
  av[k++] = "a1test";
  av[k++] = "argc";
  for (i = 2; i < nfixed; i++)
    av[k++] = "z";
  av[k] = 0;
  return av;
}

void
testxargs(void)
{
  int i;

  // The spec's examples.
  check("xargs: spec echo x", ARGV("xargs", "echo", "x"), "a b\n",
        "x a b\n", "", 0, 0);
  check("xargs: spec -n 2", XECHO("-n", "2"), "a b c d e\n",
        "x a b\nx c d\nx e\n", "", 0, 0);

  // Without -n: one run per non-empty line.
  check("xargs: one run per line", ARGV("xargs", "echo", "x"),
        "a b\nc\nd e f\n", "x a b\nx c\nx d e f\n", "", 0, 0);
  check("xargs: blank lines skipped", ARGV("xargs", "echo", "x"),
        "a\n\n   \n\t\nb\n", "x a\nx b\n", "", 0, 0);
  check("xargs: spaces and tabs", ARGV("xargs", "echo", "x"),
        "  a\t\tb  \n", "x a b\n", "", 0, 0);
  check("xargs: no trailing newline", ARGV("xargs", "echo", "x"), "a b",
        "x a b\n", "", 0, 0);
  check("xargs: empty input", ARGV("xargs", "echo", "x"), "", "", "", 0, 0);
  check("xargs: only whitespace", ARGV("xargs", "echo", "x"), " \n\t\n\n",
        "", "", 0, 0);
  check("xargs: no fixed args", ARGV("xargs", "echo"), "hi there\n",
        "hi there\n", "", 0, 0);

  // With -n: line boundaries are ignored.
  check("xargs: -n across lines", XECHO("-n", "2"), "a\nb c\nd\n",
        "x a b\nx c d\n", "", 0, 0);
  check("xargs: -n 1", XECHO("-n", "1"), "a b\nc\n", "x a\nx b\nx c\n",
        "", 0, 0);
  check("xargs: -n 3 leftover", XECHO("-n", "3"), "a b c d\n",
        "x a b c\nx d\n", "", 0, 0);
  check("xargs: -n bigger than input", XECHO("-n", "10"), "a\nb\n",
        "x a b\n", "", 0, 0);
  check("xargs: -n no trailing newline", XECHO("-n", "2"), "a b c",
        "x a b\nx c\n", "", 0, 0);
  check("xargs: -n blank lines", XECHO("-n", "2"), "a\n\n\nb\n",
        "x a b\n", "", 0, 0);
  check("xargs: -n empty input", XECHO("-n", "2"), "", "", "", 0, 0);

  // Exit status: 0 only if every run exited 0; later runs still happen.
  check("xargs: all runs succeed", ARGV("xargs", "a1test", "fail"),
        "a\nb\n", "a\nb\n", "", 0, 0);
  check("xargs: middle run fails", ARGV("xargs", "a1test", "fail"),
        "good\nbad\nlater\n", "good\nlater\n", "", 1, 0);
  check("xargs: last run fails", ARGV("xargs", "a1test", "fail"),
        "a\nbad\n", "a\n", "", 1, 0);
  check("xargs: -n run fails", ARGV("xargs", "-n", "2", "a1test", "fail"),
        "a bad c d\n", "c d\n", "", 1, 0);
  check("xargs: missing command", ARGV("xargs", "nosuchcmd"), "a\n",
        "", 0, 1, 0);
  check("xargs: missing command, no input", ARGV("xargs", "nosuchcmd"), "",
        "", "", 0, 0);

  // Bad usage.
  check("xargs: no args", ARGV("xargs"), "a\n", "", XUSAGE, 1, 0);
  check("xargs: -n 0", XECHO("-n", "0"), "a\n", "", XUSAGE, 1, 0);
  check("xargs: -n -1", XECHO("-n", "-1"), "a\n", "", XUSAGE, 1, 0);
  check("xargs: -n x", XECHO("-n", "x"), "a\n", "", XUSAGE, 1, 0);
  check("xargs: -n 2x", XECHO("-n", "2x"), "a\n", "", XUSAGE, 1, 0);
  check("xargs: -n empty", XECHO("-n", ""), "a\n", "", XUSAGE, 1, 0);
  check("xargs: -n without N", ARGV("xargs", "-n"), "a\n", "", XUSAGE, 1, 0);
  check("xargs: -n without cmd", ARGV("xargs", "-n", "2"), "a\n", "",
        XUSAGE, 1, 0);

  // MAXARG: exec takes at most 31 arguments, so runs are split to fit.
  wbuf[0] = '\0';
  for (i = 0; i < 40; i++)
    append(wbuf, i < 39 ? "w " : "w\n");
  check("xargs: 40 words split at 31", ARGV("xargs", "a1test", "argc"), wbuf,
        "argc=31\nargc=13\n", "", 0, 0);
  check("xargs: -n 50 capped at 31", ARGV("xargs", "-n", "50", "a1test", "argc"),
        wbuf, "argc=31\nargc=13\n", "", 0, 0);
  check("xargs: -n 29 exactly fits", ARGV("xargs", "-n", "29", "a1test", "argc"),
        wbuf, "argc=31\nargc=13\n", "", 0, 0);
  check("xargs: -n 10 below cap", ARGV("xargs", "-n", "10", "a1test", "argc"),
        wbuf, "argc=12\nargc=12\nargc=12\nargc=12\n", "", 0, 0);
  check("xargs: 30 fixed args", xargsfixed(0, 30), "p q\n",
        "argc=31\nargc=31\n", "", 0, 0);
  check("xargs: 28 fixed args, -n 5", xargsfixed("5", 28), "1 2 3 4\n",
        "argc=31\nargc=29\n", "", 0, 0);
}

struct group {
  char *name;
  void (*fn)(void);
} groups[] = {
  {"sleep", testsleep},
  {"find", testfind},
  {"xargs", testxargs},
  {0, 0},
};

int
main(int argc, char *argv[])
{
  struct group *g;
  int i, found;

  if (argc >= 2 && strcmp(argv[1], "argc") == 0) {
    printf("argc=%d\n", argc);
    exit(0);
  }
  if (argc >= 2 && strcmp(argv[1], "fail") == 0) {
    for (i = 2; i < argc; i++) {
      if (strcmp(argv[i], "bad") == 0)
        exit(1);
    }
    for (i = 2; i < argc; i++)
      printf("%s%s", argv[i], i + 1 < argc ? " " : "\n");
    exit(0);
  }

  chdir("/");
  rmtree(TMPDIR);
  if (mkdir(TMPDIR) < 0) {
    fprintf(2, "a1test: cannot create %s\n", TMPDIR);
    exit(1);
  }

  found = 0;
  for (g = groups; g->name; g++) {
    if (argc < 2 || strcmp(argv[1], g->name) == 0) {
      found = 1;
      printf("== %s\n", g->name);
      g->fn();
      chdir("/");
    }
  }
  if (argc >= 2 && !found) {
    printf("A1TEST: unknown group %s\n", argv[1]);
    exit(1);
  }

  rmtree(TMPDIR);
  if (nfail)
    printf("A1TEST: %d FAILED, %d passed\n", nfail, npass);
  else
    printf("A1TEST: ALL PASSED (%d)\n", npass);
  exit(nfail ? 1 : 0);
}
