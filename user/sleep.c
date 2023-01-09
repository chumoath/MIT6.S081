#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// the .c will be compiler on the work directory
//    so use the user/user.h, if compile on cur dir, use user.h

int
main(int argc, char * argv[])
{
    if(argc != 2){
        fprintf(2, "Usage: sleep [num]\n");
        exit(1);
    }

    if(sleep(atoi(argv[1])) < 0){
        fprintf(2, "sleep: sleep %d failed \n", atoi(argv[1]));
        exit(1);
    }

    exit(0);
}