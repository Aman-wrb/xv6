#include "param.h"
#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"
#include "syscall.h"
#include "traps.h"
#include "memlayout.h"

int main(int argc, char *argv[])
{
    int wtime;
    int rtime;

    printf(1, "Testing waitx()...\n");

    int pid = fork();
    if(pid == 0) //child process
    {
      exec(argv[1], argv + 1);
      exit();      
    }

    waitx(&wtime,&rtime);

    cprintf("Waiting time: %d\n", wtime);
    cprintf("Running time: %d\n", rtime);
    cprintf("Address of Wtime %ld",&wtime);
    cprintf("Address of Rtime %ld",&rtime);

    exit();

    return 0;
}
