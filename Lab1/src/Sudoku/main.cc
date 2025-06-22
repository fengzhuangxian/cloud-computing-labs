#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <iostream>
#include <vector>
#include <queue>
#include <thread>
#include <pthread.h>

#include "sudoku.h"
using namespace std;

#define BUFFER_SIZE 100000

typedef struct{
  string str_puzzle;
  int board[N];
  bool solved=false;//题是否已解
  bool ready=false;//是否有题
}puzzle_t;

//全局数独题
puzzle_t puzzles[BUFFER_SIZE];
//存放文件名
queue<string> input_queue;

int input_index = 0;
int solve_index = 0;
int output_index = 0;

// int64_t now()
// {
//   struct timeval tv;
//   gettimeofday(&tv, NULL);
//   return tv.tv_sec * 1000000 + tv.tv_usec;
// }

//vector<pair<string,bool>> puzzle_array;
pthread_mutex_t array_mutex=PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t work_available_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t input_available_cond = PTHREAD_COND_INITIALIZER;
//int current_index =0;


pthread_mutex_t input_mutex=PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t output_cond = PTHREAD_COND_INITIALIZER;
//int head_index =0;

//输入
void* input_thread(void* arg){
  string filename;
  while(true){
      cin>>filename;
      pthread_mutex_lock(&input_mutex);
      // printf("输入");
      input_queue.push(filename);
      pthread_mutex_unlock(&input_mutex);
  }
  return nullptr;
}

//读取文件:1.读取文件名；2.写入数独数组
void* read_file_thread(void* arg){
  while(true){
      string filename;
      pthread_mutex_lock(&input_mutex);
      if(!input_queue.empty()){
          filename = input_queue.front();
          input_queue.pop();
          // printf("读取文件名");
      }
      pthread_mutex_unlock(&input_mutex);

      if(!filename.empty()){
          FILE* fp = fopen(filename.c_str(), "r");
          if(fp !=NULL){
              // printf("读取文件");
              char puzzle[128];
              while (fgets(puzzle, sizeof puzzle, fp) != NULL) {
                if (strlen(puzzle) >= N) {
                  pthread_mutex_lock(&array_mutex);

                  //此时缓冲区满，不能再加题
                  while(puzzles[input_index].ready){
                    pthread_cond_wait(&input_available_cond,&array_mutex);
                  }

                  //将读取的数独题存入缓冲区,以字符串形式
                  puzzles[input_index].str_puzzle = puzzle;
                  puzzles[input_index].ready = true;
                  input_index= (input_index + 1) % BUFFER_SIZE;

                  //唤醒解题线程解题
                  pthread_cond_signal(&work_available_cond);
                  pthread_mutex_unlock(&array_mutex);
                }
              }
          fclose(fp);
      }
      }


  }
  return nullptr;
}

void input(puzzle_t &puzzle)
{
  for (int cell = 0; cell < N; ++cell) {
    puzzle.board[cell] = puzzle.str_puzzle[cell] - '0';
    assert(0 <= puzzle.board[cell] && puzzle.board[cell] <= NUM);
  }
}

//调用具体解数独方法的线程函数：1.修改数组；2.唤醒输出线程;3被唤醒
void* solve_puzzles(void* arg){
  bool (*solve)(int*) = solve_sudoku_dancing_links;
  while(true){
      //vector<pair<string,bool>> local_puzzles;
      int local_start_index;
      int local_end_index;
      pthread_mutex_lock(&array_mutex);
      while(!puzzles[solve_index].ready){
          pthread_cond_wait(&work_available_cond,&array_mutex);
      }
      // printf("读取数独");

      //抢题，做题范围为[start,end),一次最多抢10道
      local_start_index = solve_index;
      local_end_index= local_start_index;

      for(int i=0;i<10 && puzzles[local_end_index].ready;++i){
          local_end_index = (local_end_index + 1) % BUFFER_SIZE;
      }

      solve_index = local_end_index;

      pthread_mutex_unlock(&array_mutex);
      //没有题
      if(local_start_index == local_end_index){
          break;
      }
      
      //SudokuState state;
      //解题
      for(int i=local_start_index;i!=local_end_index;i=(i+1)%BUFFER_SIZE){
          // printf("解数独");
          string& puzzle=puzzles[i].str_puzzle;
          if(puzzle.length()>=N){
              input(puzzles[i]);
              if(solve(puzzles[i].board)){
                  //puzzles[i].solved=true;
              }else{
                  printf("No:%s\n",puzzle.c_str());
              }
          }
      }
      //解完10题之后再标记解完,唤醒输出线程
      pthread_mutex_lock(&array_mutex);
      for(int i=local_start_index;i!=local_end_index;i=(i+1)%BUFFER_SIZE){
        puzzles[i].solved=true;
      }
      pthread_cond_signal(&output_cond);
      pthread_mutex_unlock(&array_mutex);
  }
  return nullptr;
}

//输出
void* output_thread(void* arg){
  while(true){
      pthread_mutex_lock(&array_mutex);
      while(!puzzles[output_index].solved){
          pthread_cond_wait(&output_cond,&array_mutex);
      }
      for(int i=0;i<N;i++)printf("%d",puzzles[output_index].board[i]);
      printf("\n");
      puzzles[output_index].solved=false;
      puzzles[output_index].ready=false;
      output_index = (output_index + 1) % BUFFER_SIZE;
      pthread_cond_signal(&input_available_cond);
      pthread_mutex_unlock(&array_mutex);

  }
  return nullptr;
}

// void* output_thread(void* arg){
//   const int print_size = 10;
//   while(true){
//       pthread_mutex_lock(&array_mutex);


//       while(!puzzles[output_index].solved){
//           pthread_cond_wait(&output_cond,&array_mutex);
//       }
      

//       for(int j=0;j<print_size;++j){
//         if(!puzzles[output_index].solved){
//           break;
//         }

//         for(int i=0;i<N;i++)printf("%d",puzzles[output_index].board[i]);
//         printf("\n");

//         puzzles[output_index].solved=false;
//         puzzles[output_index].ready=false;
//         output_index = (output_index + 1) % BUFFER_SIZE;
//       }
      
//       pthread_cond_signal(&input_available_cond);
//       pthread_mutex_unlock(&array_mutex);

//   }
//   return nullptr;
// }

int main()
{
  int num_cores = thread::hardware_concurrency();
  if(num_cores == 0){
    num_cores =1;
  }

  //printf("num_cores:%d\n",num_cores);  

  pthread_t input_tid,read_tid,output_tid;
  vector<pthread_t> threads(num_cores-4);
  pthread_create(&input_tid,nullptr,input_thread,nullptr);
  pthread_create(&read_tid,nullptr,read_file_thread,nullptr);
  pthread_create(&output_tid,nullptr,output_thread,nullptr);
  for(int i=0;i<num_cores-4;++i){
    pthread_create(&threads[i],nullptr,solve_puzzles,nullptr);
  }

  for(int i=0;i<num_cores-4;++i){
    pthread_join(threads[i],nullptr);
  }
  pthread_join(input_tid,nullptr);
  pthread_join(read_tid,nullptr);
  pthread_join(output_tid,nullptr);

  return 0;
}



