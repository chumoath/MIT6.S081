#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int i;

  if(argc < 2){
    fprintf(2, "Usage: rm files...\n");
    exit(1);
  }

  /* hard link
        only is deleted when all name is unlink and the fd is closed

        open("/tmp/xyz", O_CREAT | O_WRONLY);
        unlink("/tmp/xyz")
        to create a temporary file
            the inode will be delete when the fd is closed
   */
  for(i = 1; i < argc; i++){
    if(unlink(argv[i]) < 0){
      fprintf(2, "rm: %s failed to delete\n", argv[i]);
      break;
    }
  }

  exit(0);
}
