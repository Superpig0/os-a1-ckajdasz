#include "kernel/types.h"
#include "user/user.h"

static void
usage(void)
{
  fprintf(2, "usage: sleep <ticks>\n");
  exit(1);
}

int
main(int argc, char *argv[])
{
  char *s;

  if (argc != 2 || argv[1][0] == '\0')
    usage();
  for (s = argv[1]; *s; s++) {
    if (*s < '0' || *s > '9')
      usage();
  }

  pause(atoi(argv[1]));
  exit(0);
}
