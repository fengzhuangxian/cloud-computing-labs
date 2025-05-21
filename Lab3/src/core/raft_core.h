#ifndef RAFT_CORE_H
#define RAFT_CORE_H

#include <atomic>
#include <mutex>
#include <vector>
#include <thread>
#include <condition_variable>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <chrono>
#include <random>

#include "../include/constants.h"
#include "../storage/log_store.h"
#include "../storage/kv_store.h"
#include "../network/message.h"
#include "../utils/tools.h"

namespace raft {


// Raft节点状态枚举
enum class NodeState {
    FOLLOWER,
    CANDIDATE,
    LEADER
};

/**
 * RaftCore类 - Raft算法核心实现
 */
class RaftCore {
public:
    /**
     * 回调函数类型定义
     */
    using SendMessageCallback = std::function<bool(int target_id, const Message& message)>;
    
    /**
     * 构造函数
     * @param node_id 节点ID（由上层传入，通常为端口号尾数）
     * @param cluster_size 集群大小（由上层根据配置文件节点数传入）
     * @param log_store 日志存储
     * @param kv_store KV存储
     */
    RaftCore(int node_id, int cluster_size, LogStore* log_store, KVStore* kv_store);
    
    /**
     * 析构函数
     */
    ~RaftCore();
    
    /**
     * 启动Raft服务
     */
    void start();
    
    /**
     * 停止Raft服务
     */
    void stop();
    
    /**
     * 处理接收到的消息
     * @param from_node_id 发送者节点ID
     * @param message 收到的消息
     * @return 响应消息，如果无需响应则返回nullptr
     */
    std::unique_ptr<Message> handleMessage(int from_node_id, const Message& message);
    
    /**
     * 设置发送消息回调
     * @param callback 回调函数
     */
    void setSendMessageCallback(SendMessageCallback callback) {
        send_message_callback_ = callback;
    }
    
    /**
     * 获取当前状态
     * @return 当前状态
     */
    NodeState getState() const { 
        return state_.load();
    }
    
    /**
     * 获取当前任期
     * @return 当前任期
     */
    int getCurrentTerm() const { return current_term_; }
    
    /**
     * 获取Leader ID
     * @return Leader ID，如果没有Leader则返回0
     */
    int getLeaderId() const { return leader_id_; }
    
    /**
     * 获取最后应用的日志索引
     * @return 最后应用的日志索引
     */
    int getLastApplied() const { return last_applied_; }
    
    /**
     * 获取已提交的日志索引
     * @return 已提交的日志索引
     */
    int getCommitIndex() const { return commit_index_; }
    
    /**
     * 设置最后应用的日志索引
     * @param index 索引
     */
    void setLastApplied(int index) { last_applied_ = index; }
    
    /**
     * 获取集群中除了自己外的所有节点ID列表
     * @return 其他节点ID列表
     */
    std::vector<int> getPeerNodeIds() const;
    
    /**
     * 将节点ID转换为内部数组索引
     * @param node_id 节点ID
     * @return 该节点在内部数组中的索引
     */
    int nodeIdToIndex(int node_id) const;
    
    /**
     * 添加日志条目
     * @param command 命令内容
     * @param term 任期
     * @return 添加的日志索引
     */
    int appendLogEntry(const std::string& command, int term);
    
    /**
     * 检查节点是否为Leader
     * @return 是否为Leader
     */
    bool isLeader() const { return state_.load() == NodeState::LEADER; }
    
private:
    /**
     * 主循环
     */
    void mainLoop();
    
    /**
     * Follower状态循环
     */
    void followerLoop();
    
    /**
     * Candidate状态循环
     */
    void candidateLoop();
    
    /**
     * Leader状态循环
     */
    void leaderLoop();
    
    /**
     * 日志应用循环
     */
    // void logApplierLoop(); // 日志应用功能已移至RaftNode
    
    
    /**
     * 心跳线程
     */
    void heartbeatThread();
    
    
    /**
     * 成为领导者
     */
    void becomeLeader();
    
    /**
     * 成为跟随者
     * @param term 新任期
     */
    void becomeFollower(int term);
    
  
    
    /**
     * 成为候选人
     */
    void becomeCandidate();
    
    /**
     * 处理RequestVote请求
     * @param from_node_id 发送者节点ID
     * @param request 请求消息
     * @return 响应消息
     */
    std::unique_ptr<Message> handleRequestVote(int from_node_id, const RequestVoteRequest& request);
    
    /**
     * 处理RequestVote响应
     * @param from_node_id 发送者节点ID
     * @param response 响应消息
     */
    void handleRequestVoteResponse(int from_node_id, const RequestVoteResponse& response);
    
    /**
     * 处理AppendEntries请求
     * @param from_node_id 发送者节点ID
     * @param request 请求消息
     * @return 响应消息
     */
    std::unique_ptr<Message> handleAppendEntries(int from_node_id, const AppendEntriesRequest& request);
    
    /**
     * 处理AppendEntries响应
     * @param from_node_id 发送者节点ID
     * @param response 响应消息
     */
    void handleAppendEntriesResponse(int from_node_id, const AppendEntriesResponse& response);
    
    
    /**
     * 更新提交索引
     */
    void updateCommitIndex();
    
    /**
     * 发送RequestVote请求到指定节点
     * @param target_id 目标节点ID
     */
    void sendRequestVote(int target_id);
    
    /**
     * 发送AppendEntries请求到指定节点
     * @param target_id 目标节点ID
     * @param is_heartbeat 是否是心跳
     */
    void sendAppendEntries(int target_id, bool is_heartbeat = false);
    
    /**
     * 发送消息
     * @param target_id 目标节点ID
     * @param message 消息
     * @return 是否发送成功
     */
    bool sendMessage(int target_id, const Message& message);
    
private:
    // 基本信息
    int id_;                                    // 节点ID
    int cluster_size_;                          // 集群大小
    
    // 组件指针
    LogStore* log_store_;                       // 日志存储
    KVStore* kv_store_;                         // KV存储
    
    // 状态
    std::atomic<NodeState> state_;              // 当前状态
    std::atomic<int> current_term_;             // 当前任期
    std::atomic<bool> voted_;                   // 是否已投票
    std::atomic<int> vote_count_;               // 获得的票数
    std::atomic<int> leader_id_;                // 领导者ID
    std::atomic<bool> received_heartbeat_;      // 是否收到心跳
    
    //领导选举相关
    std::mutex vote_mutex_;                    // 保护投票状态的互斥锁
    
    // 日志复制相关
    std::atomic<int> commit_index_;             // 已提交的日志索引
    std::atomic<int> last_applied_;             // 最后应用的日志索引
    std::atomic<int> leader_commit_index_;      // 领导者的提交索引
    std::vector<int> match_index_;              // 每个节点已复制的最高日志索引
    std::vector<int> match_term_;               // 每个节点已复制的最高日志任期
    std::mutex match_mutex_;                    // 保护match_index_和match_term_的互斥锁
    std::atomic<int> ack_;                      // 当前收到的确认号
    std::atomic<int> seq_;                      // 当前请求序列号
    
    // 日志应用相关
    std::mutex log_apply_mutex_;                // 日志应用互斥锁
    std::condition_variable log_apply_cv_;      // 日志应用条件变量
    
    // 响应计数
    std::atomic<int> response_node_count_;      // 已响应的节点数
   
    
    // 心跳相关
    std::atomic<int> live_count_;               // 存活计数(避免网络分区)
    
    // 线程相关
    std::atomic<bool> running_;                 // 是否运行中
    std::thread main_loop_thread_;              // 主循环线程
    // std::thread log_applier_thread_;         // 日志应用线程已移至RaftNode
    
    // 回调函数
    SendMessageCallback send_message_callback_; // 发送消息回调
};

} // namespace raft

#endif // RAFT_CORE_H 