#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(void)
{
  int parent_to_child[2];
  int child_to_parent[2];
  int pid;
  char byte = 'x';

  if(pipe(parent_to_child) < 0){
    fprintf(2, "pingpong: parent-to-child pipe failed\n");
    exit(1);
  }
  if(pipe(child_to_parent) < 0){
    fprintf(2, "pingpong: child-to-parent pipe failed\n");
    close(parent_to_child[0]);
    close(parent_to_child[1]);
    exit(1);
  }

  pid = fork();
  if(pid < 0){
    fprintf(2, "pingpong: fork failed\n");
    close(parent_to_child[0]);
    close(parent_to_child[1]);
    close(child_to_parent[0]);
    close(child_to_parent[1]);
    exit(1);
  }

  if(pid == 0){
    close(parent_to_child[1]);
    close(child_to_parent[0]);

    if(read(parent_to_child[0], &byte, 1) != 1){
      fprintf(2, "pingpong: child read failed\n");
      close(parent_to_child[0]);
      close(child_to_parent[1]);
      exit(1);
    }
    close(parent_to_child[0]);

    printf("%d: received ping\n", getpid());

    if(write(child_to_parent[1], &byte, 1) != 1){
      fprintf(2, "pingpong: child write failed\n");
      close(child_to_parent[1]);
      exit(1);
    }
    close(child_to_parent[1]);
    exit(0);
  }

  close(parent_to_child[0]);
  close(child_to_parent[1]);

  if(write(parent_to_child[1], &byte, 1) != 1){
    fprintf(2, "pingpong: parent write failed\n");
    close(parent_to_child[1]);
    close(child_to_parent[0]);
    wait(0);
    exit(1);
  }
  close(parent_to_child[1]);

  if(read(child_to_parent[0], &byte, 1) != 1){
    fprintf(2, "pingpong: parent read failed\n");
    close(child_to_parent[0]);
    wait(0);
    exit(1);
  }
  close(child_to_parent[0]);

  printf("%d: received pong\n", getpid());

  if(wait(0) < 0){
    fprintf(2, "pingpong: wait failed\n");
    exit(1);
  }
  exit(0);
}
