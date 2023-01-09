#include <unistd.h>

/* shell process tree 
    a | b | c
        shell
          |
     -----O-----
     |         |
     a     ----O-----
           |        |
           b        c
    the leaves is command, the internal node is process to wait two process for exit    

    right must wait until left close the write end of pipe
           due to this, implement the rule of left to right
    
    1. pipeline ensure the sequence is from left to right
    2. the pipefile is deleted automatically
    3. the pipeline is a arbitrarily long streams of data
    4. all commands can execve in the same time based on from left to right

    pipe sequence:
        a stdin  =>  stdin
          stdout =>  p1[1]

        b stdin  =>  p1[0]
          stdout =>  p2[1]

        c stdin  =>  p2[0]
          stdout =>  stdout

    wait sequence:
        a send data to p1[1], then close p1[1]
        b wait a for close p1[1]
        b read data from p1[0], then send data to p2[1], then close p2[1]
        c wait b for close p2[1], then read data from p2[0]

    the redirect is controlled by the internal node conveniently
        if not interal process, the control flow will be complicated 
*/

int
main(void)
{
    int p[2];
    char * argv[2];
    char ** eargv;

    argv[0] = "/bin/wc";
    argv[1] = 0;

    /* eargv is a pointer variable, place the char* */
    eargv = 0; 

    pipe(p);

    // function:
    //     child execve wc, read data from the pipe, the pipe connect to the parent

    if(fork() == 0){
        /* redirect to stdin */
        close(0);
        dup(p[0]);
        
        /* close the unnecessary fd */
        close(p[0]);

        // must close the wirte end, because write end close, the read end can reveive end-of-file
        //     this ask all process's fd refered to this write end is closed
        close(p[1]);
        
        // wc: print newline word bytes count
        //    when the left close the write end, the right will unblocked to continue, so two is execve in the same time
        execve(argv[0], argv, eargv);
    } else {
    

    // parent pass the data to child by pipe
        close(p[0]);
        write(p[1], "hello world\n", 12);
        close(p[1]);
    }
}