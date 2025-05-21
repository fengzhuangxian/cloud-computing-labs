#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <functional>
#include <memory>
#include <sys/epoll.h>
#include <netinet/in.h>

#include "message.h"
#include "message_handler.h"
#include "../include/constants.h"
#include "../utils/thread_pool.h"

namespace raft {

// 端口类型枚举
enum class PortType {
    CLIENT = 1,  // 客户端通信端口
    RAFT = 2     // Raft内部通信端口
};

// 节点配置
struct NodeConfig {
    int id;                // 节点ID
    std::string ip;        // 节点IP地址
    int port;              // 基本端口号（client端口）
};

// 消息处理回调函数类型
using MessageCallback = std::function<std::unique_ptr<Message>(int from_node_id, const Message& message)>;
// 客户端请求处理回调函数类型
using ClientRequestCallback = std::function<std::string(int client_fd, const std::string& request)>;

/**
 * NetworkManager类 - 负责处理所有网络通信
 */
class NetworkManager {
public:
    /**
     * 构造函数
     * @param node_id 当前节点ID（由RaftNode提供）
     * @param config_path 配置文件路径（NetworkManager负责解析所有端口和网络配置）
     */
    NetworkManager(int node_id, const std::string& config_path);
    
    /**
     * 析构函数
     */
    ~NetworkManager();
    
    /**
     * 启动网络服务
     * @return 是否成功启动
     */
    bool start();
    
    /**
     * 停止网络服务
     */
    void stop();
    
    /**
     * 设置消息处理回调
     * @param callback 消息处理函数
     */
    void setMessageCallback(MessageCallback callback) { message_callback_ = callback; }
    
    /**
     * 设置客户端请求处理回调
     * @param callback 客户端请求处理函数
     */
    void setClientRequestCallback(ClientRequestCallback callback) { client_request_callback_ = callback; }
    
    /**
     * 向指定节点发送消息
     * @param target_id 目标节点ID
     * @param message 要发送的消息
     * @return 是否发送成功
     */
    bool sendMessage(int target_id, const Message& message);
    
    /**
     * 向客户端发送响应
     * @param client_fd 客户端连接描述符
     * @param response 响应内容
     * @return 是否发送成功
     */
    bool sendClientResponse(int client_fd, const std::string& response);
    
    /**
     * 获取当前节点ID
     * @return 当前节点ID
     */
    int getNodeId() const { return self_id_; }
    
    /**
     * 获取集群大小
     * @return 集群中的节点数量
     */
    int getClusterSize() const { return 1 + peers_.size(); }

    /**
     * 异步处理客户端请求
     * @param client_fd 客户端连接描述符
     * @param request 请求内容
     */
    void asyncProcessClientRequest(int client_fd, const std::string& request);

    /**
     * 异步处理Raft消息
     * @param fd 连接描述符
     * @param from_node_id 发送者节点ID
     * @param message 消息
     */
    void asyncProcessRaftMessage(int fd, int from_node_id, std::unique_ptr<Message> message);

private:
    // 配置信息
    int self_id_;                                  // 当前节点ID
    int client_port_;                              // 客户端端口
    int raft_port_;                                // Raft内部通信端口
    std::vector<NodeConfig> peers_;                // 其他节点配置
    
    // 网络状态
    std::atomic<bool> running_;                    // 是否正在运行
    int client_listen_fd_;                         // 客户端监听套接字
    int raft_listen_fd_;                           // Raft内部通信监听套接字
    int epoll_fd_;                                 // epoll文件描述符
    
    // 连接管理
    std::mutex connections_mutex_;                 // 连接互斥锁
    std::unordered_map<int, PortType> fd_types_;   // 文件描述符到端口类型的映射
    std::unordered_map<int, int> fd_to_node_id_;   // 文件描述符到节点ID的映射
    std::unordered_map<int, int> node_id_to_fd_;   // 节点ID到文件描述符的映射
    std::unordered_map<int, std::string> receive_buffers_; // 文件描述符到接收缓冲区的映射
    
    // 回调函数
    MessageCallback message_callback_;             // 消息处理回调
    ClientRequestCallback client_request_callback_;// 客户端请求处理回调
    
    // 线程
    std::thread network_thread_;                   // 网络事件处理线程
    std::thread reconnect_thread_;                 // 重连线程
    std::mutex send_mutex_;                        // 发送锁

    // 线程池
    std::unique_ptr<ThreadPool> thread_pool_;      // 客户端请求处理线程池
    std::unique_ptr<ThreadPool> raft_thread_pool_; // Raft消息处理线程池
    
    // 私有辅助方法
    bool parseConfig(const std::string& config_path);  // 解析配置文件
    bool initNetwork();                            // 初始化网络
    void networkLoop();                            // 网络事件循环
    bool handleNewConnection(int listen_fd, PortType port_type);  // 处理新连接
    bool processSocketData(int fd);                // 处理socket数据
    bool connectToPeer(int node_id);               // 连接到对等节点
    void closeConnection(int fd);                  // 关闭连接
    NodeConfig* getPeerConfig(int node_id);        // 获取节点配置
    int getClientPort() const { return client_port_; } // 获取客户端端口
    int getRaftPort() const { return raft_port_; }     // 获取Raft端口
};

} // namespace raft

#endif // NETWORK_MANAGER_H 