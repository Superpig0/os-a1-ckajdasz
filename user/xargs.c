#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

#define MAXWORD 512

static char *args[MAXARG + 1];
static int nbase;    // cmd and its fixed args, at the front of args[]
static int nwords;   // words read from stdin, after the fixed args
static int failed;

static void
usage(void)
{
  fprintf(2, "usage: xargs [-n N] cmd [args...]\n");
  exit(1);
}

// Run the command with the pending words, wait for it, and free the
// words.
static void
run(void)
{
  int pid, status, i;

  if (nwords == 0)
    return;
  args[nbase + nwords] = 0;

  pid = fork();
  if (pid < 0) {
    fprintf(2, "xargs: fork failed\n");
    failed = 1;
  } else if (pid == 0) {
    exec(args[0], args);
    fprintf(2, "xargs: exec %s failed\n", args[0]);
    exit(1);
  } else {
    if (wait(&status) < 0 || status != 0)
      failed = 1;
  }

  for (i = nbase; i < nbase + nwords; i++) {
    free(args[i]);
    args[i] = 0;
  }
  nwords = 0;
}

static void
addword(char *w, int len)
{
  char *s;

  if ((s = malloc(len + 1)) == 0) {
    fprintf(2, "xargs: out of memory\n");
    exit(1);
  }
  memmove(s, w, len);
  s[len] = '\0';
  args[nbase + nwords++] = s;
}

int
main(int argc, char *argv[])
{
  int i, first, n, cap, len;
  char c, word[MAXWORD];
  char *s;

  first = 1;
  n = 0; // 0 means no -n: one run per line
  if (argc > 1 && strcmp(argv[1], "-n") == 0) {
    if (argc < 3 || argv[2][0] == '\0')
      usage();
    for (s = argv[2]; *s; s++) {
      if (*s < '0' || *s > '9')
        usage();
    }
    n = atoi(argv[2]);
    if (n < 1)
      usage();
    first = 3;
  }
  if (first >= argc)
    usage();

  // exec takes at most MAXARG-1 arguments: sys_exec's argv[MAXARG]
  // also has to hold the terminating null.
  nbase = argc - first;
  if (nbase >= MAXARG - 1) {
    fprintf(2, "xargs: too many arguments\n");
    exit(1);
  }
  for (i = 0; i < nbase; i++)
    args[i] = argv[first + i];

  // Most words one run can take and still be accepted by exec.
  cap = MAXARG - 1 - nbase;
  if (n > 0 && n < cap)
    cap = n;

  len = 0;
  for (;;) {
    int r = read(0, &c, 1);
    int sep = r < 1 || c == ' ' || c == '\t' || c == '\n';

    if (!sep) {
      if (len < MAXWORD)
        word[len++] = c;
      continue;
    }
    if (len > 0) {
      addword(word, len);
      len = 0;
      if (nwords == cap)
        run();
    }
    if (r < 1)
      break;
    if (c == '\n' && n == 0)
      run();
  }
  run();

  exit(failed);
}
