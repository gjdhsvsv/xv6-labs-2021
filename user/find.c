#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

static char*
basename(char *path)
{
  char *p;

  for(p = path + strlen(path); p >= path && *p != '/'; p--)
    ;
  return p + 1;
}

static void
find(char *path, char *target)
{
  char buf[512];
  char name[DIRSIZ + 1];
  char *p;
  int fd;
  int n;
  int needs_slash;
  uint path_len;
  struct dirent de;
  struct stat st;

  if((fd = open(path, 0)) < 0){
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  if(st.type != T_DIR){
    if(strcmp(basename(path), target) == 0)
      printf("%s\n", path);
    close(fd);
    return;
  }

  path_len = strlen(path);
  needs_slash = path_len > 0 && path[path_len - 1] != '/';
  if(path_len + needs_slash + 1 > sizeof(buf)){
    fprintf(2, "find: path too long %s\n", path);
    close(fd);
    return;
  }

  strcpy(buf, path);
  p = buf + path_len;
  if(needs_slash)
    *p++ = '/';

  while((n = read(fd, &de, sizeof(de))) == sizeof(de)){
    if(de.inum == 0)
      continue;

    memmove(name, de.name, DIRSIZ);
    name[DIRSIZ] = '\0';
    if(strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
      continue;

    if((uint)(p - buf) + strlen(name) + 1 > sizeof(buf)){
      fprintf(2, "find: path too long %s/%s\n", path, name);
      continue;
    }

    strcpy(p, name);
    if(stat(buf, &st) < 0){
      fprintf(2, "find: cannot stat %s\n", buf);
      continue;
    }

    if(strcmp(name, target) == 0)
      printf("%s\n", buf);

    if(st.type == T_DIR)
      find(buf, target);
  }

  if(n != 0)
    fprintf(2, "find: cannot read %s\n", path);
  close(fd);
}

int
main(int argc, char *argv[])
{
  if(argc != 3){
    fprintf(2, "Usage: find path name\n");
    exit(1);
  }

  find(argv[1], argv[2]);
  exit(0);
}
