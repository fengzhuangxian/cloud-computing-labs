#ifndef SUDOKU_H
#define SUDOKU_H

const bool DEBUG_MODE = false;
enum { ROW=9, COL=9, N = 81, NEIGHBOR = 20 }; //20包含同行8个 + 同列8个 + 同九宫格4个(除去重复的行列)
const int NUM = 9;

// extern int neighbors[N][NEIGHBOR]; 
// extern int board[N];
// extern int spaces[N];
// extern int nspaces;
// extern int (*chess)[COL];

// void init_neighbors();

//将输入的81个字符转化出board，spaces，nspaces和chess
// void input(const char in[N]);

// void init_cache();

//对于一维索引为cell的空格，填入guess是否会与邻居冲突
// bool available(int guess, int cell);

// bool solve_sudoku_basic(int which_space);
// bool solve_sudoku_min_arity(int which_space);
// bool solve_sudoku_min_arity_cache(int which_space);
bool solve_sudoku_dancing_links(int* board);

//检查当前chess棋盘是否为有效解
// bool solved();
#endif

