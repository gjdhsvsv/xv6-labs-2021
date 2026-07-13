#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static void
sieve(int input_fd)
{
  int prime;
  int value;
  int next_pipe[2];
  int pid;
  int n;

  n = read(input_fd, &prime, sizeof(prime));
  if(n == 0){
    close(input_fd);
    exit(0);
  }
  if(n != sizeof(prime)){
    fprintf(2, "primes: read failed\n");
    close(input_fd);
    exit(1);
  }

  printf("prime %d\n", prime);

  if(pipe(next_pipe) < 0){
    fprintf(2, "primes: pipe failed\n");
    close(input_fd);
    exit(1);
  }

  pid = fork();
  if(pid < 0){
    fprintf(2, "primes: fork failed\n");
    close(input_fd);
    close(next_pipe[0]);
    close(next_pipe[1]);
    exit(1);
  }

  if(pid == 0){
    close(input_fd);
    close(next_pipe[1]);
    sieve(next_pipe[0]);
    exit(0);
  }

  close(next_pipe[0]);
  while((n = read(input_fd, &value, sizeof(value))) == sizeof(value)){
    if(value % prime != 0){
      if(write(next_pipe[1], &value, sizeof(value)) != sizeof(value)){
        fprintf(2, "primes: write failed\n");
        close(input_fd);
        close(next_pipe[1]);
        wait(0);
        exit(1);
      }
    }
  }

  close(input_fd);
  close(next_pipe[1]);

  if(n != 0){
    fprintf(2, "primes: read failed\n");
    wait(0);
    exit(1);
  }
  if(wait(0) < 0){
    fprintf(2, "primes: wait failed\n");
    exit(1);
  }
  exit(0);
}

int
main(void)
{
  int first_pipe[2];
  int pid;
  int value;

  if(pipe(first_pipe) < 0){
    fprintf(2, "primes: pipe failed\n");
    exit(1);
  }

  pid = fork();
  if(pid < 0){
    fprintf(2, "primes: fork failed\n");
    close(first_pipe[0]);
    close(first_pipe[1]);
    exit(1);
  }

  if(pid == 0){
    close(first_pipe[1]);
    sieve(first_pipe[0]);
    exit(0);
  }

  close(first_pipe[0]);
  for(value = 2; value <= 35; value++){
    if(write(first_pipe[1], &value, sizeof(value)) != sizeof(value)){
      fprintf(2, "primes: write failed\n");
      close(first_pipe[1]);
      wait(0);
      exit(1);
    }
  }
  close(first_pipe[1]);

  if(wait(0) < 0){
    fprintf(2, "primes: wait failed\n");
    exit(1);
  }
  exit(0);
}
