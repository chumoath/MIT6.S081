#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "user/user.h"


void
find(const char * path, const char * filename)
{
    // 可能是相对目录，所以 stat 和 open 的 路径 都要 以 cur dir 为基准   
    int fd;
    char buf[512], *p;
    struct stat st;
    struct dirent de;


    // open this path
    if((fd = open(path, O_RDONLY)) < 0){
        fprintf(2, "find: can not open %s\n", path);
        return;
    }

    // get this path stat
    if(fstat(fd, &st) < 0){
        fprintf(2, "find: can not stat %s\n", path);
        close(fd);
        return;
    }


    // check this path's type
    switch(st.type){
    
    // file => can not find
    case T_FILE:
        fprintf(2, "find: %s is not a directory\n", path);
        return;

    // dir  => find
    case T_DIR:
        // path => buf
        strcpy(buf, path);
        // p => path/
        p = buf+strlen(path);
        *p++ = '/';

        
        while(read(fd, &de, sizeof de) == sizeof de) {
            // special dir, refer to ls.c
            if(de.inum == 0)
                continue;

            // can not recurse the . and ..
            if(!strcmp(de.name, ".") || !strcmp(de.name, ".."))
                continue;
            
            // p => path/name
            strcpy(p, de.name);
            
            // fprintf(1, "%s\n", buf);

            // get this dirent's stat
            if(stat(buf, &st) < 0){
                fprintf(2, "find: can not stat %s\n", buf);
                continue;
            }

            // check this dirent's type
            switch(st.type){
            
            // dir => recurse
            case T_DIR:
                find(buf, filename);
                break;

            // file => check whether or not
            case T_FILE:
                // filename is same, print
                if(!strcmp(p, filename))
                    fprintf(1, "%s\n", buf);
                
                break;
            }
        }

        // fprintf(1, "here, quit loop\n");

        break;
    }


    close(fd);
}


int
main(int argc, char * argv[])
{
    if(argc != 3)
    {
        fprintf(2, "Usage: find [dir] [filename]\n");
        exit(1);
    }

    find(argv[1], argv[2]);

    fprintf(2, "find finish\n");
    exit(0);
}