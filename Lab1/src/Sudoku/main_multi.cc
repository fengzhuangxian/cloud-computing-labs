#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <pthread.h>
#include "sudoku.h"
#include<stdlib.h>
#include <thread>
using namespace std;
 typedef struct {
   int first;
   int last;
   int** tqa;//问题和结果数组(test question and answer)
  }threadtask;
 
void* threadfun(void* args){ //这个是线程运行函数，每个线程解决一部分数独题目
threadtask* task = (threadtask*) args;
    int first=task->first;
    int last=task->last;
    int** tqa=task->tqa;
    int i=0;
 
    for(i=first;i<last;i++){
    solve_sudoku_dancing_links(tqa[i]);
 
}
    pthread_exit(NULL);
    return NULL;
}
 
 
 
int main(int argc, char* argv[])
{
    char testfile[128]= "\0";
    while(scanf("%s",testfile)){//接收文件路径输入
        char puzzle[128];
 
    FILE* f1 = fopen(testfile, "r");
    if (!f1) {
        perror("无法打开文件");
        continue;  // 跳过错误文件
    }
    int testnumber=0;
    while (fgets(puzzle, sizeof(puzzle), f1) != NULL) {
        testnumber++;//统计题目数量
    }
    fclose(f1);
 
    unsigned int num_cores = thread::hardware_concurrency();
    int threadnumber=num_cores;//线程数
 
    int i=0;
    int row = testnumber,col = N;
    //申请
    int **testqa = (int**)malloc(sizeof(int*) * row);  //sizeof(int*),不能少*，一个指针的内存大小，每个元素是一个指针。
    for (i = 0;i < row;i++)
    {
        testqa[i] = (int*)malloc(sizeof(int) * col);//保存结果的数组
    }
 
 
 
 
    FILE* fp = fopen(testfile, "r");
    i=0;
    char tt[testnumber][128];//临时数组
    while (fgets(tt[i], sizeof tt[i], fp) != NULL) {
        i++;
    }
 
 
    //输入转换
    for(int t1=0;t1<testnumber;t1++){
        for (int t2 = 0; t2 < N; t2++) {
            testqa[t1][t2] = (int)(tt[t1][t2] - '0');
        }
    }
 
 
    int pertask= testnumber/threadnumber;
    threadtask threadgroup[threadnumber];
    pthread_t th[threadnumber]; 
 
    if(pertask>=1){//为每个线程分配任务
 
   for(i=0;i<threadnumber;i++)
   {
     int first=(int)pertask*i;//开始平均分配任务
     int last;
     if(i!=threadnumber-1)
       last=(int)pertask*(i+1);
     else
       last=testnumber;
 
 
     threadgroup[i].first=first;
     threadgroup[i].last=last;
     threadgroup[i].tqa=testqa;
 
     if(pthread_create(&th[i], NULL, threadfun, &threadgroup[i])!=0)
     {//创建线程
       perror("pthread_create failed");
       exit(1);
     }
 
   }
 
   for(i=0;i<threadnumber;i++)
   pthread_join(th[i], NULL);
 
 
 
}
else{//一个线程情况
 threadgroup[0].first=0;
     threadgroup[0].last=testnumber;
     threadgroup[0].tqa=testqa;
     if(pthread_create(&th[0], NULL, threadfun, &threadgroup[0])!=0)
     {
       perror("pthread_create failed");
       exit(1);
     }
pthread_join(th[0], NULL);
}
 
 
 
 
int j=0;
int k=0;
//输出结果
for(j=0;j<testnumber;j++){
for(k=0;k<N;k++){
printf("%d",testqa[j][k]);
}
printf("\n");
}
 
 
free(testqa);
fflush(stdout);
}
  return 0;
}