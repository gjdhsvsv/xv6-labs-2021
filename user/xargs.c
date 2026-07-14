#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"

#define LINE_SIZE 512

static int
is_space(char c)
{
  return c == ' ' || c == '\t' || c == '\r';
}

static int
run_line(char *line, int length, int argc, char *argv[])
{
  char *args[MAXARG];
  char *p;
  char *end;
  int arg_count;
  int base_count;
  int i;
  int pid;
  int status;

  base_count = argc - 1;
  arg_count = base_count;
  for(i = 0; i < base_count; i++)
    args[i] = argv[i + 1];

  line[length] = '\0';
  p = line;
  end = line + length;
  while(p < end){
    while(p < end && is_space(*p))
      p++;
    if(p == end)
      break;
    if(arg_count >= MAXARG - 1){
      fprintf(2, "xargs: too many arguments\n");
      return -1;
    }

    args[arg_count++] = p;
    while(p < end && !is_space(*p))
      p++;
    if(p < end)
      *p++ = '\0';
  }

  if(arg_count == base_count)
    return 0;
  args[arg_count] = 0;

  pid = fork();
  if(pid < 0){
    fprintf(2, "xargs: fork failed\n");
    return -1;
  }
  if(pid == 0){
    exec(args[0], args);
    fprintf(2, "xargs: exec %s failed\n", args[0]);
    exit(1);
  }

  if(wait(&status) < 0){
    fprintf(2, "xargs: wait failed\n");
    return -1;
  }
  if(status != 0)
    return -1;
  return 0;
}

int
main(int argc, char *argv[])
{
  char line[LINE_SIZE];
  char c;
  int length;
  int n;

  if(argc < 2){
    fprintf(2, "Usage: xargs command [arguments ...]\n");
    exit(1);
  }
  if(argc - 1 >= MAXARG){
    fprintf(2, "xargs: too many command arguments\n");
    exit(1);
  }

  length = 0;
  while((n = read(0, &c, 1)) == 1){
    if(c == '\n'){
      if(run_line(line, length, argc, argv) < 0)
        exit(1);
      length = 0;
      continue;
    }

    if(length >= LINE_SIZE - 1){
      fprintf(2, "xargs: input line too long\n");
      exit(1);
    }
    line[length++] = c;
  }

  if(n < 0){
    fprintf(2, "xargs: read failed\n");
    exit(1);
  }
  if(length > 0 && run_line(line, length, argc, argv) < 0)
    exit(1);

  exit(0);
}
