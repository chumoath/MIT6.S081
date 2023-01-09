#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

void execCommand(char ** newargv);

int
main(int argc, char * argv[])
{
    if(argc < 2){
        fprintf(2, "Usage: xargs command [arguments...]\n");
        exit(1);
    }

    char * newargv[MAXARG];
    char ** arg;

    int i;
    for(i = 0; i + 1 < argc; ++i)
        newargv[i] = argv[i + 1];

    // 将指针解释为引用
    // arg           => 要引用一个 char*
    // &newargv[i]   => 传递一个 char* 到引用

    // 该位置的字符串动态改变
    arg = &newargv[i];

    // argv 结尾 NULL
    newargv[i + 1] = 0;

    char buf[512];
    char * p = buf;
    int num;

    // 不能以此作为条件，因为这样，一旦一次读入很多行，然后 num == 0，就会去执行 num == 0的语句，将许多行一起处理
    while((num = read(0, p, buf + sizeof(buf) - p - 1)) > 0){
        
        // very important
        *(p + num) = '\0'; // 为了是 C 字符串

        // 处理所有的 \n
        while((p = strchr(buf, '\n')) != 0){

            *p = '\0';
            *arg = buf;
            execCommand(newargv);
            
            strcpy(buf, p + 1);
            // p = buf+strlen(buf);

        } 
        // else {
            // p = buf+strlen(buf);
        // }

        // 找到 \n，则 将 \n 后的 字符串移到 buf， 将 p 指向 \0，继续读
        // 未找到 \n，则将 p 指向 \0，继续读
        p = buf+strlen(buf);
    }

    // 最后的没有 \n 结尾的字符，也需要作为参数执行
    if(num == 0 && p != buf){
        // p != buf，即 数据
        *p = '\0';
        *arg = buf;
        execCommand(newargv);
    }

    exit(0);
}


void 
execCommand(char ** newargv)
{

    if(fork() == 0){
        // xargs 的输入是 fd[0]
        //    所以子进程的输入也是 fd[0]
        //    

        // fprintf(1, "will exec: ");

        // char ** p = newargv;

        // while(*p != 0)
        //     fprintf(1, "%s ", *p++);
        
        // fprintf(1, "\n");


        exec(newargv[0], newargv);

        fprintf(1, "xargs: can not exec %s\n", newargv[0]);
        exit(1);
    }

    wait(0);
}