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

// Remove path and, if it is a directory, everything under it.
void
rmtree(char *path)
{
  char child[256], *p;
  struct dirent *de;
  struct stat st;
  int fd, n, i;

  if (stat(path, &st) < 0)
    return;
  if (st.type == T_DIR && strlen(path) + 1 + DIRSIZ + 1 <= sizeof child) {
    if ((fd = open(path, O_RDONLY)) < 0)
      return;
    if ((de = malloc(st.size)) == 0) {
      close(fd);
      return;
    }
    n = read(fd, de, st.size) / sizeof(*de);
    close(fd);
    strcpy(child, path);
    p = child + strlen(child);
    *p++ = '/';
    for (i = 0; i < n; i++) {
      if (de[i].inum == 0)
        continue;
      memmove(p, de[i].name, DIRSIZ);
      p[DIRSIZ] = '\0';
      if (strcmp(p, ".") == 0 || strcmp(p, "..") == 0)
        continue;
      rmtree(child);
    }
    free(de);
  }
  unlink(path);
}

void
mkfile(char *path)
{
  if (writefile(path, "x\n") < 0)
    fprintf(2, "a1test: cannot create %s\n", path);
}

struct group {
  char *name;
  void (*fn)(void);
} groups[] = {
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
