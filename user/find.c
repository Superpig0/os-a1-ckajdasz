#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "kernel/fs.h"
#include "user/user.h"

// The path being visited. find() extends it in place on the way down
// and cuts it back on the way up, so deep trees use no extra stack.
static char path[512];
static char *pattern;
static int filter; // 0 for any type, 'f' or 'd' for -type

static void
usage(void)
{
  fprintf(2, "usage: find <dir> <pattern> [-type f|d]\n");
  exit(1);
}

// Does s match p? '*' matches any run of characters, including an
// empty one; every other character must match exactly.
static int
match(char *p, char *s)
{
  if (*p == '\0')
    return *s == '\0';
  if (*p == '*') {
    while (*p == '*')
      p++;
    if (*p == '\0')
      return 1;
    for (; *s; s++) {
      if (match(p, s))
        return 1;
    }
    return 0;
  }
  return *s == *p && match(p + 1, s + 1);
}

static int
wanted(short type)
{
  if (filter == 'f')
    return type == T_FILE || type == T_DEVICE;
  if (filter == 'd')
    return type == T_DIR;
  return 1;
}

// Search the directory named by path[0..len). Returns -1 if it cannot
// be opened. The directory is read whole and closed before recursing,
// so the depth of the tree never uses up file descriptors.
static int
find(int len)
{
  struct dirent *de;
  struct stat st;
  char name[DIRSIZ + 1];
  int fd, n, i, sep, nlen;

  if ((fd = open(path, O_RDONLY)) < 0)
    return -1;
  if (fstat(fd, &st) < 0 || st.type != T_DIR) {
    close(fd);
    return 0;
  }
  if ((de = malloc(st.size)) == 0) {
    fprintf(2, "find: out of memory\n");
    close(fd);
    return 0;
  }
  n = read(fd, de, st.size);
  close(fd);
  n = n < 0 ? 0 : n / sizeof(*de);

  sep = path[len - 1] != '/';
  for (i = 0; i < n; i++) {
    if (de[i].inum == 0)
      continue;
    // A name of exactly DIRSIZ characters has no terminating NUL.
    memmove(name, de[i].name, DIRSIZ);
    name[DIRSIZ] = '\0';
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
      continue;

    nlen = len + sep + strlen(name);
    if (nlen + 1 > sizeof(path)) {
      fprintf(2, "find: path too long: %s\n", path);
      continue;
    }
    if (sep)
      path[len] = '/';
    strcpy(path + len + sep, name);

    if (stat(path, &st) < 0) {
      fprintf(2, "find: cannot stat %s\n", path);
    } else {
      if (match(pattern, name) && wanted(st.type))
        printf("%s\n", path);
      if (st.type == T_DIR)
        find(nlen);
    }
    path[len] = '\0';
  }
  free(de);
  return 0;
}

int
main(int argc, char *argv[])
{
  if (argc == 5 && strcmp(argv[3], "-type") == 0 &&
      (strcmp(argv[4], "f") == 0 || strcmp(argv[4], "d") == 0))
    filter = argv[4][0];
  else if (argc != 3)
    usage();

  pattern = argv[2];
  // An empty <dir> would open the current directory; treat it as
  // unopenable, as is a name too long for the path buffer.
  if (argv[1][0] == '\0' || strlen(argv[1]) >= sizeof(path))
    goto cannotopen;
  strcpy(path, argv[1]);
  if (find(strlen(path)) < 0)
    goto cannotopen;
  exit(0);

cannotopen:
  fprintf(2, "find: cannot open %s\n", argv[1]);
  exit(1);
}
