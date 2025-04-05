#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "sudoku.h"
#include <sys/time.h>  // 高精度时间测量

// 获取当前时间戳（毫秒级精度）
double get_timestamp() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

#define N 81
#define MAX_QUEUE_SIZE 1024
#define MAX_THREADS 16

/* 全局数据结构 */
typedef struct {
    int id;             // 题目ID
    int puzzle[N];      // 数独存储
    int solved;         // 解决标志
} SudokuTask;

// 线程间共享数据
struct {
    SudokuTask* tasks;      // 任务数组
    int capacity;           // 数组容量
    int count;              // 实际题目数
    
    int prod_id;            // 生产者当前分配ID
    int cons_id;            // 消费者当前处理ID
    int output_id;          // 输出线程当前ID
    
    pthread_mutex_t lock;   // 全局锁
    pthread_cond_t cond;    // 条件变量
} shared_data;

/* 线程池结构 */
typedef struct {
    pthread_t threads[MAX_THREADS];
    int running;
} ThreadPool;

/* 初始化共享数据 */
void init_shared(int capacity) {
    shared_data.tasks = (SudokuTask *)malloc(capacity * sizeof(SudokuTask));
    shared_data.capacity = capacity;
    shared_data.count = 0;
    shared_data.prod_id = 0;
    shared_data.cons_id = 0;
    shared_data.output_id = 0;
    pthread_mutex_init(&shared_data.lock, NULL);
    pthread_cond_init(&shared_data.cond, NULL);
}

/* 文件读取线程 */
void* file_reader(void* arg) {
    char* filename = (char*)arg;
    
    FILE* fp = fopen(filename, "r");
    if (!fp) {
        perror("无法打开文件");
        return NULL;
    }

    // 单次读取完整文件
    char buffer[256];
    int line_count = 0;
    while (fgets(buffer, sizeof(buffer), fp)) {
        // 动态扩容检查
        if (line_count >= shared_data.capacity) {
            int new_cap = shared_data.capacity * 2;
            SudokuTask* new_tasks = (SudokuTask *)realloc(shared_data.tasks, 
                                      new_cap * sizeof(SudokuTask));
            if (!new_tasks) {
                perror("内存扩容失败");
                break;
            }
            shared_data.tasks = new_tasks;
            shared_data.capacity = new_cap;
        }

        // 写入任务数组
        SudokuTask* task = &shared_data.tasks[line_count];
        for (int i = 0; i < N; i++) {
            task->puzzle[i] = buffer[i] - '0';
        }
        task->id = line_count;
        task->solved = 0;
        line_count++;
    }
    
    // 更新全局计数
    pthread_mutex_lock(&shared_data.lock);
    shared_data.count = line_count;
    pthread_cond_broadcast(&shared_data.cond);
    pthread_mutex_unlock(&shared_data.lock);
    
    fclose(fp);
    return NULL;
}

/* 工作线程函数 */
void* worker_func(void* arg) {
    while (1) {
        pthread_mutex_lock(&shared_data.lock);
        
        // 等待可处理任务
        while (shared_data.cons_id >= shared_data.count) {
            pthread_cond_wait(&shared_data.cond, &shared_data.lock);
        }
        
        // 获取任务
        int task_id = shared_data.cons_id++;
        SudokuTask* task = &shared_data.tasks[task_id];
        
        pthread_mutex_unlock(&shared_data.lock);

        // 实际解题（无锁操作）
        solve_sudoku_dancing_links(task->puzzle);
        
        // 标记完成
        pthread_mutex_lock(&shared_data.lock);
        task->solved = 1;
        pthread_cond_broadcast(&shared_data.cond); // 通知输出线程
        pthread_mutex_unlock(&shared_data.lock);
    }
    return NULL;
}

/* 输出线程函数 */
void* output_func(void* arg) {
    while (1) {
        pthread_mutex_lock(&shared_data.lock);
        
        // 等待当前ID完成
        while (shared_data.output_id < shared_data.count &&
              !shared_data.tasks[shared_data.output_id].solved) {
            pthread_cond_wait(&shared_data.cond, &shared_data.lock);
        }
        
        // 按顺序输出所有已完成
        while (shared_data.output_id < shared_data.count &&
              shared_data.tasks[shared_data.output_id].solved) {
            SudokuTask* task = &shared_data.tasks[shared_data.output_id];
            for (int i = 0; i < N; i++) {
                putchar('0' + task->puzzle[i]);
            }
            putchar('\n');
            shared_data.output_id++;
        }
        
        pthread_mutex_unlock(&shared_data.lock);
        
        // 全部完成则退出
        if (shared_data.output_id >= shared_data.count) {
            break;
        }
    }
    return NULL;
}

/* 初始化线程池 */
void init_pool(ThreadPool* pool) {
    for (int i = 0; i < MAX_THREADS; i++) {
        pthread_create(&pool->threads[i], NULL, worker_func, NULL);
    }
    pool->running = 1;
}

int main(int argc, char* argv[]) {
    ThreadPool pool;
    init_shared(100);  // 初始容量100
    
    // 启动工作线程
    init_pool(&pool);

    // 主线程作为文件读取者
    char filename[256];
    while (scanf("%255s", filename) == 1) {
        double start_time = get_timestamp(); // 开始计时
        pthread_t reader;
        pthread_create(&reader, NULL, file_reader, filename);
        
        // 等待文件读取完成
        pthread_join(reader, NULL);
        
        // 启动输出线程
        pthread_t output_thread;
        pthread_create(&output_thread, NULL, output_func, NULL);
        pthread_join(output_thread, NULL);

        double end_time = get_timestamp(); // 结束计时
        double total_time = end_time - start_time;

        // 输出耗时到标准错误（避免与结果混合）
        fprintf(stderr, "文件 %s 处理耗时: %.2f 毫秒\n", 
                filename, total_time);
        
        // 重置状态
        pthread_mutex_lock(&shared_data.lock);
        shared_data.cons_id = 0;
        shared_data.output_id = 0;
        shared_data.count = 0;
        pthread_mutex_unlock(&shared_data.lock);
    }
    
    // 清理资源
    free(shared_data.tasks);
    return 0;
}