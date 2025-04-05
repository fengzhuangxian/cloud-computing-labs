#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "sudoku.h"

int64_t now()
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec * 1000000 + tv.tv_usec;
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
    int total_solved = 0;
    int total = 0;
    bool (*solve)(int) = solve_sudoku_dancing_links;
    int64_t start = now();
    while (fgets(puzzle, sizeof puzzle, f1) != NULL) {
    if (strlen(puzzle) >= N) {
      ++total;
      input(puzzle);
      init_cache();
      if (solve(0)) {
        printf("%s\n",puzzle);
        ++total_solved;
        if (!solved())
          assert(0);
      }
      else {
        printf("No: %s", puzzle);
      }
    }
  }
  int64_t end = now();
  double sec = (end-start)/1000000.0;
  printf("%f sec %f ms each %d\n", sec, 1000*sec/total, total_solved);
}

  return 0;
}

