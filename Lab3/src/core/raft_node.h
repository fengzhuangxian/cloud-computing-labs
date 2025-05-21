#ifndef RAFT_NODE_H
#define RAFT_NODE_H

#include "../core/raft_core.h"
#include "../network/network_manager.h"
#include "../storage/kv_store.h"
#include "../storage/log_store.h"
#include "../utils/redis_protocol.h"
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>

namespace raft {

/**
 * RaftNode类 - 顶层节点类，整合各个重构后的组件
 * 包括RaftCore、NetworkManager、KVStore等
 */
class RaftNode {
public:
    /**
     * 构造函数
     * @param config_path 配置文件路径（RaftNode负责解析本节点id、端口等信息，网络细节交由NetworkManager管理）
     * @param log_dir 日志目录
     */
    RaftNode(const std::string& config_path, const std::string& log_dir = "");
    
    /**
     * 析构函数，清理资源
     */
    ~RaftNode();
    
    /**
     * 启动节点服务
     */
    void Run();
    
    /**
     * 停止节点服务
     */
    void Stop();
    
private:
    /**
     * 初始化组件
     */
    bool initComponents();
    
    /**
     * 处理网络收到的消息回调
     */
    std::unique_ptr<Message> handleMessage(int from_node_id, const Message& message);
    
    /**
     * 处理客户端请求回调
     */
    std::string handleClientRequest(int client_fd, const std::string& request);
    
    /**
     * 处理RESP格式的客户端请求
     * @param client_fd 客户端连接fd
     * @param command 解析后的命令
     * @param original_request 原始请求内容
     * @return 处理结果
     */
    std::string handleRespCommand(int client_fd, const std::vector<std::string>& command, const std::string& original_request);
    
    /**
     * 处理命令应用到状态机
     */
    std::string applyCommand(const std::string& command);
    
    /**
     * 日志应用线程主循环
     * 负责将已提交的日志应用到状态机
     */
    void logApplierLoop();
    
private:
    // 基本信息
    int node_id_;                                    // 节点ID（从配置文件解析）
    std::string config_path_;                        // 配置文件路径
    std::string log_dir_;                            // 日志目录
    
    // 核心组件
    std::unique_ptr<LogStore> log_store_;            // 日志存储
    std::unique_ptr<KVStore> kv_store_;              // KV存储
    std::unique_ptr<RaftCore> raft_core_;            // Raft核心
    std::unique_ptr<NetworkManager> network_manager_; // 网络管理器
    
    // 状态标记
    std::atomic<bool> running_;                      // 运行标志
    
    // 线程
    std::thread log_apply_thread_;                   // 日志应用线程
    
    // 互斥锁
    std::mutex apply_mutex_;                         // 应用互斥锁
};

} // namespace raft

#endif // RAFT_NODE_H 