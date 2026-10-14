#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/uproc.h"
#include "user/user.h"

// Indexed by enum procstate in kernel/proc.h; ps never sees UNUSED.
static char *states[] = {
  [1] = "used",
  [2] = "sleeping",
  [3] = "runnable",
  [4] = "running",
  [5] = "zombie",
};

// Global: NPROC entries are too big for the one-page user stack.
static struct uproc procs[NPROC];

int
main(void)
{
  struct uproc *u;
  char *state;
  int n;

  if ((n = getprocs(procs, NPROC)) < 0) {
    fprintf(2, "ps: getprocs failed\n");
    exit(1);
  }
  printf("PID\tPPID\tSTATE\tSIZE\tNAME\n");
  for (u = procs; u < procs + n; u++) {
    state = "?";
    if (u->state >= 1 && u->state < sizeof(states) / sizeof(states[0]))
      state = states[u->state];
    printf("%d\t%d\t%s\t%lu\t%s\n", u->pid, u->ppid, state, u->sz, u->name);
  }
  exit(0);
}
