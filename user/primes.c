#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


int
main(int argc, char * argv[])
{
    // one process produce data
    //  another process create filter

    int p_g[2];
    pipe(p_g);

    if(fork()){
        // parent   =>  add numbers，最上层的输出者
        close(1);
        dup(p_g[1]);
        close(p_g[0]); close(p_g[1]);

        // 0 -> stdin
        // 1 -> p_g[1]

        for(int i = 2; i <= 35; ++i)
            // 传递给下一个过滤器
            write(1, &i, sizeof i);
        
        // 关闭自己的输出管道，触发 EOF，下一个 filter read 会 返回 0
        close(1);

        // 必须等待所有过滤完了才能退出，否则 终端会被释放
        // 所以，添加 数字的必须是 父进程，这样才能等待子进程
        wait(0);   // 等待 add filter 的进程退出，即所有过滤器都退出了才退出，以阻止 释放终端
        
        // 释放终端，是进程 退出后，关闭 0 进行的，即 阻塞等待输入
        //   只有 用于 输入数字的进程有 stdin 的 描述符，新创建的进程，必须有 stdin，然后 轮到他 read，才能读终端

        // 原本是 shell 在等待输入，执行 命令后，创建新进程，然后 shell 一直等待 子进程结束 (wait)
        //      所以 shell 在 子进程结束前，不能 read，所以不能使用终端，相当于 shell 挂起
        //      一旦 shell 的 子进程退出，则 shell 就会继续调用 read，从而阻塞等待输入
        //      所以，shell 执行的命令，也要阻塞等待子进程的退出，否则直接退出，就会被 shell 占据终端
        exit(0);
        
    } else {    

        // child    =>  add filter，最下层的得到输入的人
        close(0);
        dup(p_g[0]);
        close(p_g[0]); close(p_g[1]);

        // 0 -> p_g[0];
        // 1 - stdout

        

        // 创建 filter 的 进程 循环执行，获取素数，创建新的 filter


        /* 开始获取 素数，能到这里的都是素数 */
        for(;;){

            int p;
            if(read(0, &p, sizeof p) <= 0){
                close(0); // eof，在整个 pipe 链的最尾端
                exit(0);
            }

            // 打印素数 => 总是同一个进程打印，即 add filter 的 进程
            fprintf(1, "prime %d\n", p);


            // 创建新的 filter => child 是放在 pipe 链的最尾端的
            int pd[2];
            pipe(pd);


            // 父进程 必须是 add filter 的 进程，因为 它必须是输入进程的直接子进程
            //                因为该进程是最后一个退出的，所以 输入进程 必须等该进程退出后才能退出
            //                即 将 所有数字打印完了才能退出
            //                所以，必须这么安排

            if(fork()){
                // 接收 新创建的 filter 的 输出

                // parent => 到 pipe 链的尾端，继续等待新的素数，增加新的 filter
                close(0); // 不会被关闭，因为子进程已经复制了
                dup(pd[0]);
                close(pd[0]); close(pd[1]);

                // 0 -> 新的 pd[0] => 永远指向 最尾端 的 pipe 的 输入
                // 1 -> stdout

                // 继续等待素数，来创建新的 filter
                continue;   // to loop


            } else {
                // 继续接收 目前的 pipe，输出给 新的 pipe

                // child   =>    新的过滤器
                close(1);
                dup(pd[1]);
                close(pd[0]); close(pd[1]);

                // 0 -> 从 父进程 复制过来的，上一个尾 filter 的 pipe 的 输入
                // 1 -> 新的pd[1]

                // 过滤器 不断接收 上一个过滤器的 输出， 满足要求，传到下一层过滤器
                //     filter 不会执行别的代码，只会执行 该循环中的代码
                for(;;){
                    int n;
                    if(read(0, &n, sizeof n) <= 0){
                        close(1);
                        close(0);
                        exit(0);
                    }

                    // 过滤掉自己，每个 filter 有自己的 p
                    //      负责 产生 filter 的 进程，每读到一个 p，就会 更新 p，新的 filter 就会使用新的 p
                    if(n % p != 0)
                        // 传递到下一个过滤器
                        write(1, &n, sizeof n);
                }
            }

        }
    }
}