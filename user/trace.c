#include "kernel/types.h"
#include "user/user.h"

static void
usage(void)
{
  fprintf(2, "usage: trace <mask> <cmd> [args...]\n");
  exit(1);
}

int
main(int argc, char *argv[])
{
  char *s;

  if (argc < 3 || argv[1][0] == '\0')
    usage();
  for (s = argv[1]; *s; s++) {
    if (*s < '0' || *s > '9')
      usage();
  }

  if (trace(atoi(argv[1])) < 0) {
    fprintf(2, "trace: trace failed\n");
    exit(1);
  }
  exec(argv[2], &argv[2]);
  fprintf(2, "trace: exec %s failed\n", argv[2]);
  exit(1);
}
