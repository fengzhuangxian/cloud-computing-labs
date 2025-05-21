#include "network_manager.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <regex>
#include <set>

namespace raft {

// 构造函数
NetworkManager::NetworkManager(int node_id, const std::string& config_path)
    : self_id_(node_id),
      client_port_(0),
      raft_port_(0),
      running_(false),
      client_listen_fd_(-1),
      raft_listen_fd_(-1),
      epoll_fd_(-1) {
    
    // 初始化线程池
    // 客户端请求处理线程池 = 总线程数 - Raft消息处理线程数
    thread_pool_ = std::make_unique<ThreadPool>(THREAD_POOL_SIZE - RAFT_MESSAGE_THREADS);
    // Raft消息处理线程池
    raft_thread_pool_ = std::make_unique<ThreadPool>(RAFT_MESSAGE_THREADS);
    
    std::cout << "ThreadPool initialized: " 
              << (THREAD_POOL_SIZE - RAFT_MESSAGE_THREADS) << " threads for client requests, "
              << RAFT_MESSAGE_THREADS << " threads for Raft messages" << std::endl;
    
    // 解析配置文件
    if (!parseConfig(config_path)) {
        throw std::runtime_error("Failed to parse config file: " + config_path);
    }
    
    // 处理SIGPIPE信号
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    if (sigaction(SIGPIPE, &sa, NULL)) {
        throw std::runtime_error("Failed to set SIGPIPE handler");
    }
    
    std::cout << "NetworkManager initialized with node ID: " << self_id_
              << ", client port: " << client_port_
              << ", raft port: " << raft_port_ << std::endl;
}

// 析构函数
NetworkManager::~NetworkManager() {
    // 停止网络服务
    stop();
    
    // 线程池会在unique_ptr析构时自动销毁
}

// 解析配置文件
bool NetworkManager::parseConfig(const std::string& config_path) {
    std::ifstream file(config_path);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file: " << config_path << std::endl;
        return false;
    }
    
    // 根据节点ID，计算默认端口
    client_port_ = 8000 + self_id_; // 默认端口：8000+ID
    raft_port_ = client_port_ - 1000; // Raft端口 = 客户端端口 - 1000
    
    std::string line;
    std::regex follower_regex("follower_info\\s+(\\S+):(\\d+)");
    std::smatch match;
    int line_count = 0;
    
    while (std::getline(file, line)) {
        line_count++;
        // 跳过空行和注释行
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // 解析 follower_info 行
        if (std::regex_search(line, match, follower_regex)) {
            std::string ip = match[1];
            int port = std::stoi(match[2]);
            
            // 第一行是本节点信息
            if (line_count == 1) {
                client_port_ = port;
                raft_port_ = port - 1000; // Raft端口 = 客户端端口 - 1000
                
                // 验证节点ID是否与端口尾数匹配
                if (self_id_ != port % 10) {
                    std::cerr << "Warning: Node ID " << self_id_ << " does not match port " << port << " last digit" << std::endl;
                }
            } else {
                // 添加到其他节点配置列表
                NodeConfig peer;
                peer.id = port % 10; // 节点ID为端口尾数
                peer.ip = ip;
                peer.port = port;
                peers_.push_back(peer);
            }
        }
    }
    
    return true;
}

// 启动网络服务
bool NetworkManager::start() {
    if (running_) {
        return true;  // 已经运行
    }
    
    // 初始化网络
    if (!initNetwork()) {
        std::cerr << "Failed to initialize network" << std::endl;
        return false;
    }
    
    running_ = true;
    
    // 启动网络事件处理线程
    network_thread_ = std::thread(&NetworkManager::networkLoop, this);
    
    // 尝试连接到其他节点
    for (const auto& peer : peers_) {
        connectToPeer(peer.id);
    }
    
    // 启动重连线程，定期尝试重连未连接的节点
    reconnect_thread_ = std::thread([this]() {
        std::set<int> failed_nodes;
        while (running_) {
            // 获取所有未连接的节点
            {
                std::lock_guard<std::mutex> lock(connections_mutex_);
                failed_nodes.clear();
                for (const auto& peer : peers_) {
                    if (node_id_to_fd_.find(peer.id) == node_id_to_fd_.end()) {
                        failed_nodes.insert(peer.id);
                    }
                }
            }
            
            // 尝试重连
            for (int node_id : failed_nodes) {
                //std::cout << "Trying to reconnect to node " << node_id << std::endl;
                connectToPeer(node_id);
            }
            
            // 等待一段时间再重试
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
    });
    
    std::cout << "Network manager started" << std::endl;
    return true;
}

// 停止网络服务
void NetworkManager::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    // 等待线程结束
    if (network_thread_.joinable()) {
        network_thread_.join();
    }
    
    // 等待重连线程结束
    if (reconnect_thread_.joinable()) {
        reconnect_thread_.join();
    }
    
    // 关闭所有连接
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (auto& pair : fd_types_) {
            close(pair.first);
        }
        fd_types_.clear();
        fd_to_node_id_.clear();
        node_id_to_fd_.clear();
    }
    
    // 关闭监听套接字和epoll
    if (client_listen_fd_ != -1) {
        close(client_listen_fd_);
        client_listen_fd_ = -1;
    }
    
    if (raft_listen_fd_ != -1) {
        close(raft_listen_fd_);
        raft_listen_fd_ = -1;
    }
    
    if (epoll_fd_ != -1) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }
    
    std::cout << "Network manager stopped" << std::endl;
}

// 初始化网络
bool NetworkManager::initNetwork() {
    // 创建epoll实例
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) {
        std::cerr << "Failed to create epoll: " << strerror(errno) << std::endl;
        return false;
    }
    
    // 创建客户端监听socket
    client_listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (client_listen_fd_ == -1) {
        std::cerr << "Failed to create client socket: " << strerror(errno) << std::endl;
        close(epoll_fd_);
        return false;
    }
    
    // 设置socket选项
    int opt = 1;
    if (setsockopt(client_listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        std::cerr << "Failed to set client socket option: " << strerror(errno) << std::endl;
        close(client_listen_fd_);
        close(epoll_fd_);
        return false;
    }
    
    // 绑定客户端端口
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(client_port_);
    
    if (bind(client_listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        std::cerr << "Failed to bind client socket: " << strerror(errno) << std::endl;
        close(client_listen_fd_);
        close(epoll_fd_);
        return false;
    }
    
    // 监听客户端连接
    if (listen(client_listen_fd_, SOMAXCONN) == -1) {
        std::cerr << "Failed to listen on client socket: " << strerror(errno) << std::endl;
        close(client_listen_fd_);
        close(epoll_fd_);
        return false;
    }
    
    // 创建Raft监听socket
    raft_listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (raft_listen_fd_ == -1) {
        std::cerr << "Failed to create raft socket: " << strerror(errno) << std::endl;
        close(client_listen_fd_);
        close(epoll_fd_);
        return false;
    }
    
    // 设置socket选项
    if (setsockopt(raft_listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        std::cerr << "Failed to set raft socket option: " << strerror(errno) << std::endl;
        close(client_listen_fd_);
        close(raft_listen_fd_);
        close(epoll_fd_);
        return false;
    }
    
    // 绑定Raft端口
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(raft_port_);
    
    if (bind(raft_listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        std::cerr << "Failed to bind raft socket: " << strerror(errno) << std::endl;
        close(client_listen_fd_);
        close(raft_listen_fd_);
        close(epoll_fd_);
        return false;
    }
    
    // 监听Raft连接
    if (listen(raft_listen_fd_, SOMAXCONN) == -1) {
        std::cerr << "Failed to listen on raft socket: " << strerror(errno) << std::endl;
        close(client_listen_fd_);
        close(raft_listen_fd_);
        close(epoll_fd_);
        return false;
    }
    
    // 将监听socket添加到epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = client_listen_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_listen_fd_, &ev) == -1) {
        std::cerr << "Failed to add client socket to epoll: " << strerror(errno) << std::endl;
        close(client_listen_fd_);
        close(raft_listen_fd_);
        close(epoll_fd_);
        return false;
    }
    
    ev.data.fd = raft_listen_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, raft_listen_fd_, &ev) == -1) {
        std::cerr << "Failed to add raft socket to epoll: " << strerror(errno) << std::endl;
        close(client_listen_fd_);
        close(raft_listen_fd_);
        close(epoll_fd_);
        return false;
    }
    
    return true;
}

// 网络事件循环
void NetworkManager::networkLoop() {
    const int MAX_EVENTS = 32;
    struct epoll_event events[MAX_EVENTS];
    
    while (running_) {
        int nfds = epoll_wait(epoll_fd_, events, MAX_EVENTS, 100); // 100ms超时
        
        if (nfds == -1) {
            if (errno != EINTR) { // 忽略被信号中断的情况
                std::cerr << "epoll_wait error: " << strerror(errno) << std::endl;
            }
            continue;
        }
        
        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;
            
            if (fd == client_listen_fd_) {
                // 有新的客户端连接
                handleNewConnection(fd, PortType::CLIENT);
            } else if (fd == raft_listen_fd_) {
                // 有新的Raft节点连接
                handleNewConnection(fd, PortType::RAFT);
            } else if (events[i].events & EPOLLIN) {
                // 可读事件
                processSocketData(fd);
            } else if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                // 连接断开或错误
                closeConnection(fd);
            }
        }
    }
}

// 处理新连接
bool NetworkManager::handleNewConnection(int listen_fd, PortType port_type) {
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    
    int fd = accept(listen_fd, (struct sockaddr*)&addr, &addr_len);
    if (fd == -1) {
        std::cerr << "Failed to accept connection: " << strerror(errno) << std::endl;
        return false;
    }
    
    // 设置非阻塞
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        std::cerr << "Failed to set non-blocking mode: " << strerror(errno) << std::endl;
        close(fd);
        return false;
    }
    
    // 添加到epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
        std::cerr << "Failed to add socket to epoll: " << strerror(errno) << std::endl;
        close(fd);
        return false;
    }
    
    // 记录连接信息
    std::lock_guard<std::mutex> lock(connections_mutex_);
    fd_types_[fd] = port_type;
    
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip_str, sizeof(ip_str));
    std::cout << "New connection from " << ip_str << ":" << ntohs(addr.sin_port)
              << " on " << (port_type == PortType::CLIENT ? "client" : "raft") << " port" << std::endl;
    
    return true;
}

// 处理socket数据
bool NetworkManager::processSocketData(int fd) {
    PortType port_type;
    
    // 获取连接类型
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto it = fd_types_.find(fd);
        if (it == fd_types_.end()) {
            return false;
        }
        port_type = it->second;
    }
    
    // 获取或创建此连接的接收缓冲区
    auto& buffer = receive_buffers_[fd];
    
    if (port_type == PortType::CLIENT) {
        // 处理客户端请求
        auto result = MessageHandler::readClientRequest(fd, buffer);
        if (!result.first) {
            closeConnection(fd);
            return false;
        }
        
        const std::string& request = result.second;
        if (!request.empty()) {
            // 异步处理客户端请求
            asyncProcessClientRequest(fd, request);
        }
    } else {
        // 处理Raft消息
        auto result = MessageHandler::readRaftMessages(fd, buffer);
        if (!result.first) {
            closeConnection(fd);
            return false;
        }
        
        // 处理所有接收到的消息
        for (auto& message : result.second) {
            // 确定消息来源节点ID
            int from_node_id = -1;
            
            // 首先尝试从已保存的映射中获取节点ID
            {
                std::lock_guard<std::mutex> lock(connections_mutex_);
                auto it = fd_to_node_id_.find(fd);
                if (it != fd_to_node_id_.end()) {
                    from_node_id = it->second;
                }
            }
            
            // 如果节点ID未知，尝试从消息中提取
            if (from_node_id == -1) {
                // 如果是AppendEntries请求，从请求中获取leader_id
                if (message->getType() == MessageType::APPENDENTRIES_REQUEST) {
                    const auto& request = static_cast<const AppendEntriesRequest&>(*message);
                    from_node_id = request.leader_id;
                }
                // 如果是RequestVote请求，从请求中获取candidate_id
                else if (message->getType() == MessageType::REQUESTVOTE_REQUEST) {
                    const auto& request = static_cast<const RequestVoteRequest&>(*message);
                    from_node_id = request.candidate_id;
                }
                // 如果是AppendEntries响应，从响应中获取follower_id
                else if (message->getType() == MessageType::APPENDENTRIES_RESPONSE) {
                    const auto& response = static_cast<const AppendEntriesResponse&>(*message);
                    from_node_id = response.follower_id;
                }
                
                // 如果能够确定节点ID，更新映射
                if (from_node_id > 0) {
                    std::lock_guard<std::mutex> lock(connections_mutex_);
                    fd_to_node_id_[fd] = from_node_id;
                    node_id_to_fd_[from_node_id] = fd;
                }
            }
            
            // 异步处理Raft消息
            if (from_node_id > 0) {
                asyncProcessRaftMessage(fd, from_node_id, std::move(message));
            }
        }
    }
    
    return true;
}

// 连接到对等节点
bool NetworkManager::connectToPeer(int node_id) {
    // 找到对应节点的配置
    NodeConfig* peer_config = getPeerConfig(node_id);
    if (!peer_config) {
        std::cerr << "Unknown node ID: " << node_id << std::endl;
        return false;
    }
    
    // 检查是否已存在连接
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto it = node_id_to_fd_.find(node_id);
        if (it != node_id_to_fd_.end()) {
            return true; // 已连接
        }
    }
    
    // 创建socket
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
        return false;
    }
    
    // 设置非阻塞
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        std::cerr << "Failed to set non-blocking mode: " << strerror(errno) << std::endl;
        close(fd);
        return false;
    }
    
    // 连接到对等节点的Raft端口
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(peer_config->port - 1000); // Raft端口 = 客户端端口 - 1000
    
    if (inet_pton(AF_INET, peer_config->ip.c_str(), &addr.sin_addr) <= 0) {
        std::cerr << "Invalid IP address: " << peer_config->ip << std::endl;
        close(fd);
        return false;
    }
    
    // 尝试连接
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        if (errno != EINPROGRESS) {
            //std::cerr << "Failed to connect to peer " << node_id << ": " << strerror(errno) << std::endl;
            close(fd);
            return false;
        }
        
        // 非阻塞连接，使用select等待连接完成
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        
        struct timeval tv;
        tv.tv_sec = 5;  // 5秒超时
        tv.tv_usec = 0;
        
        int ret = select(fd + 1, NULL, &wfds, NULL, &tv);
        if (ret <= 0) {
            std::cerr << "Connection to peer " << node_id << " timed out or error" << std::endl;
            close(fd);
            return false;
        }
        
        // 检查连接是否成功
        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
            //std::cerr << "Failed to connect to peer " << node_id << ": "<< (error ? strerror(error) : strerror(errno)) << std::endl;
            close(fd);
            return false;
        }
    }
    
    // 添加到epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
        std::cerr << "Failed to add socket to epoll: " << strerror(errno) << std::endl;
        close(fd);
        return false;
    }
    
    // 添加连接信息
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        fd_types_[fd] = PortType::RAFT;
        fd_to_node_id_[fd] = node_id;
        node_id_to_fd_[node_id] = fd;
    }
    
    std::cout << "Connected to peer " << node_id << " at " << peer_config->ip
              << ":" << (peer_config->port - 1000) << std::endl;
    
    return true;
}

// 关闭连接
void NetworkManager::closeConnection(int fd) {
    if (fd < 0) {
        return;
    }
    
    // 从epoll移除
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, NULL);
    
    // 更新连接映射
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        // 先检查是否有关联的节点ID
        auto it_node = fd_to_node_id_.find(fd);
        if (it_node != fd_to_node_id_.end()) {
            int node_id = it_node->second;
            auto it_fd = node_id_to_fd_.find(node_id);
            if (it_fd != node_id_to_fd_.end() && it_fd->second == fd) {
                node_id_to_fd_.erase(node_id);
            }
            fd_to_node_id_.erase(it_node);
        }
        
        // 移除端口类型映射
        fd_types_.erase(fd);
        
        // 清理接收缓冲区
        receive_buffers_.erase(fd);
    }
    
    // 关闭socket
    close(fd);
}

// 向指定节点发送消息
bool NetworkManager::sendMessage(int target_id, const Message& message) {
    if (target_id == self_id_) {
        // 不向自己发送消息，直接调用回调处理
        if (message_callback_) {
            message_callback_(self_id_, message);
        }
        return true;
    }
    
    int fd = -1;
    bool need_reconnect = false;
    
    // 获取连接fd
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto it = node_id_to_fd_.find(target_id);
        if (it != node_id_to_fd_.end()) {
            fd = it->second;
        } else {
            need_reconnect = true;
        }
    }
    
    // 如果需要重连，尝试立即连接
    if (need_reconnect) {
        if (!connectToPeer(target_id)) {
            return false;  // 连接失败
        }
        
        // 重新获取fd
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto it = node_id_to_fd_.find(target_id);
        if (it == node_id_to_fd_.end()) {
            return false;  // 仍然没有连接
        }
        fd = it->second;
    }
    
    // 发送消息
    std::lock_guard<std::mutex> lock(send_mutex_);
    bool success = MessageHandler::sendRaftMessage(fd, message);
    
    // 如果发送失败，关闭连接并标记为需要重连
    if (!success) {
        closeConnection(fd);
        std::lock_guard<std::mutex> lock(connections_mutex_);
        node_id_to_fd_.erase(target_id);
    }
    
    return success;
}

// 向客户端发送响应
bool NetworkManager::sendClientResponse(int client_fd, const std::string& response) {
    if (client_fd < 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(send_mutex_);
    return MessageHandler::sendClientResponse(client_fd, response);
}

// 获取节点配置
NodeConfig* NetworkManager::getPeerConfig(int node_id) {
    for (auto& peer : peers_) {
        if (peer.id == node_id) {
            return &peer;
        }
    }
    return nullptr;
}

// 异步处理客户端请求
void NetworkManager::asyncProcessClientRequest(int client_fd, const std::string& request) {
    // 将请求处理任务提交到线程池
    thread_pool_->enqueue([this, client_fd, request]() {
        // 在工作线程中处理请求
        if (client_request_callback_) {
            try {
                std::string response = client_request_callback_(client_fd, request);
                if (!response.empty()) {
                    sendClientResponse(client_fd, response);
                }
            } catch (const std::exception& e) {
                std::cerr << "Error processing client request: " << e.what() << std::endl;
                // 发送错误响应
                sendClientResponse(client_fd, "-ERR Internal server error\r\n");
            }
        }
    });
}

// 异步处理Raft消息
void NetworkManager::asyncProcessRaftMessage(int fd, int from_node_id, std::unique_ptr<Message> message) {
    // 创建消息的副本，因为我们需要将其移动到lambda中
    Message* msg_ptr = message.release();
    
    // 将消息处理任务提交到线程池
    raft_thread_pool_->enqueue([this, fd, from_node_id, msg_ptr]() {
        // 在工作线程中处理消息
        std::unique_ptr<Message> msg(msg_ptr); // 重新获取所有权
        
        if (message_callback_) {
            try {
                auto response = message_callback_(from_node_id, *msg);
                if (response) {
                    sendMessage(from_node_id, *response);
                }
            } catch (const std::exception& e) {
                std::cerr << "Error processing Raft message: " << e.what() << std::endl;
            }
        }
    });
}

} // namespace raft 