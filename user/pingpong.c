#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char * argv[])
{
    int p[2];

    if(pipe(p) < 0){
        fprintf(2, "pipe: error]n");
        exit(1);
    }

    if(fork() == 0){
        // child
        char c;

        // block => only return 0 if p[1] entity is closed
        read(p[0], &c, 1);

        fprintf(1, "%d: received ping\n", getpid());

        write(p[1], &c, 1);

        // fd from dup-like
        close(p[0]);
        close(p[1]);

        exit(0);
    }

    // parent

    char c = 'A';
    write(p[1], &c, 1);
    
    // ensure the byte was read by parent itself
    wait(0);

    read(p[0], &c, 1);

    fprintf(1, "%d: received pong\n", getpid());

    close(p[0]);
    close(p[1]);

    exit(0);
}