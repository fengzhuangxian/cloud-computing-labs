#ifndef CONSTANTS_H
#define CONSTANTS_H

namespace raft {

// 网络相关常量
constexpr int MAX_BUFFER_SIZE = 4096;        // 最大缓冲区大小
constexpr int MAX_CONNECTION_QUEUE = 10;     // 最大连接队列长度
constexpr int MAX_EVENT = 20;                // epoll一次处理的最大事件数
constexpr int EPOLL_TIMEOUT_MS = 100;        // epoll等待超时时间(ms)

// 线程池相关常量
constexpr int THREAD_POOL_SIZE = 4;          // 线程池大小
constexpr int RAFT_MESSAGE_THREADS = 2;      // 保留给Raft消息处理的线程数
constexpr int TASK_QUEUE_MAX_SIZE = 1000;    // 任务队列最大大小

// 端口类型
constexpr int PORT_TYPE_CLIENT = 1;          // 客户端端口类型
constexpr int PORT_TYPE_RAFT = 2;            // Raft内部通信端口类型

// Raft算法相关常量
constexpr int FOLLOWER_TIMEOUT_MS = 3000;  // 选举超时最小值(ms)
constexpr int ELECTION_TIMEOUT_MIN_MS = 1000;  // 选举超时最小值(ms)
constexpr int ELECTION_TIMEOUT_MAX_MS = 3000;  // 选举超时最大值(ms)
constexpr int HEARTBEAT_INTERVAL_MS = 500;        // 心跳间隔(ms)
constexpr int LEADER_RESILIENCE_COUNT = 1;    // Leader弹性计数
constexpr int BATCH_SIZE = 10;                // 日志批处理大小

// 日志应用相关常量
constexpr int LOG_APPLY_INTERVAL_MS = 100;     // 日志应用检查间隔(ms)
//constexpr int MAX_APPLY_BATCH = 100;          // 一次最多应用的日志条数

// 超时与重试相关常量
constexpr int COMMAND_WAIT_TIMEOUT_MS = 5000; // 命令等待超时时间(ms)
constexpr int MAX_RETRY_COUNT = 3;            // 最大重试次数

} // namespace raft

#endif // CONSTANTS_H 