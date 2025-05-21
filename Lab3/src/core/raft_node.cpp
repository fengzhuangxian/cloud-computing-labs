#include "raft_node.h"
#include "../utils/tools.h"
#include "../utils/redis_protocol.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#include <regex>
#include <algorithm>

namespace raft {

// 构造函数
RaftNode::RaftNode(const std::string& config_path, const std::string& log_dir)
    : config_path_(config_path),
      log_dir_(log_dir),
      running_(false) {
    // 解析配置文件，仅获取本节点ID
    std::ifstream conf(config_path_);
    if (!conf.is_open()) {
        throw std::runtime_error("无法打开配置文件: " + config_path_);
    }
    std::string first_line;
    std::getline(conf, first_line);
    conf.close();
    std::smatch match;
    std::regex port_regex(R"((\d+\.\d+\.\d+\.\d+):(\d+))"); // 匹配ip:port
    node_id_ = 0;
    if (std::regex_search(first_line, match, port_regex) && match.size() >= 3) {
        std::string port_str = match[2];
        if (!port_str.empty()) {
            int client_port = std::stoi(port_str);
            node_id_ = client_port % 10;
        }
    }
    if (node_id_ == 0) {
        throw std::runtime_error("无法从配置文件第一行解析出节点ID，或节点ID为0");
    }
    
    // 初始化组件
    if (!initComponents()) {
        throw std::runtime_error("Failed to initialize components");
    }
    std::cout << "RaftNode initialized with ID: " << node_id_ << std::endl;
}

// 析构函数
RaftNode::~RaftNode() {
    Stop();
}

// 启动节点服务
void RaftNode::Run() {
    if (running_) {
        return;  // 已经运行中
    }
    
    // 启动网络管理器
    if (!network_manager_->start()) {
        std::cerr << "Failed to start network manager" << std::endl;
        return;
    }
    
    // 启动日志应用线程
    running_ = true;
    log_apply_thread_ = std::thread([this]() {
        logApplierLoop();
    });
    
    // 启动Raft核心
    raft_core_->start();
    
    std::cout << "RaftNode started" << std::endl;
}

// 停止节点服务
void RaftNode::Stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    // 停止Raft核心
    if (raft_core_) {
        raft_core_->stop();
    }
    
    // 停止网络管理器
    if (network_manager_) {
        network_manager_->stop();
    }
    
    // 等待日志应用线程结束
    if (log_apply_thread_.joinable()) {
        log_apply_thread_.join();
    }
    
    std::cout << "RaftNode stopped" << std::endl;
}

// 初始化组件
bool RaftNode::initComponents() {
    try {
        // 创建网络管理器，只传入node_id_和config_path_
        network_manager_ = std::make_unique<NetworkManager>(node_id_, config_path_);
        int cluster_size = network_manager_->getClusterSize();
        
        // 创建日志存储
        std::string log_filename;
        if (log_dir_.empty()) {
            log_filename = "node_" + std::to_string(node_id_) + "_raft_log.dat";
        } else {
            log_filename = log_dir_ + "/node_" + std::to_string(node_id_) + "_raft_log.dat";
        }
        log_store_ = std::make_unique<InMemoryLogStore>(log_filename);
        
        // 创建KV存储
        kv_store_ = std::make_unique<KVStore>();
        
        // 创建Raft核心
        raft_core_ = std::make_unique<RaftCore>(node_id_, cluster_size, log_store_.get(), kv_store_.get());
        
        // 设置网络回调
        network_manager_->setMessageCallback([this](int from_node_id, const Message& message) -> std::unique_ptr<Message> {
            return this->handleMessage(from_node_id, message);
        });
        
        network_manager_->setClientRequestCallback([this](int client_fd, const std::string& request) -> std::string {
            return this->handleClientRequest(client_fd, request);
        });
        
        // 设置Raft核心的发送消息回调
        raft_core_->setSendMessageCallback([this](int target_id, const Message& message) -> bool {
            return network_manager_->sendMessage(target_id, message);
        });
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error initializing components: " << e.what() << std::endl;
        return false;
    }
}

// 处理网络收到的消息回调
std::unique_ptr<Message> RaftNode::handleMessage(int from_node_id, const Message& message) {
    // 其他消息直接交给RaftCore处理
    return raft_core_->handleMessage(from_node_id, message);
}


// 处理客户端请求回调
std::string RaftNode::handleClientRequest(int client_fd, const std::string& request) {
    // 解析客户端请求
    std::string command = request;
    // 如果是RESP协议格式，解析命令
    if (!command.empty() && command[0] == '*') {
        std::vector<std::string> parsed = RedisProtocol::parseCommand(command);
        if (parsed.empty()) {
            return RedisProtocol::encodeError("Protocol error");
        }
        // 调用专门处理RESP命令的方法
        return handleRespCommand(client_fd, parsed, command);
    }
    // 非RESP协议格式或解析错误
    return RedisProtocol::encodeError("Protocol error");
}

// 处理RESP格式的客户端请求
std::string RaftNode::handleRespCommand(int client_fd, const std::vector<std::string>& command, const std::string& original_request) {
    // 检查节点状态
    NodeState state = raft_core_->getState();
    int leader_id = raft_core_->getLeaderId();
    
    if (state == NodeState::CANDIDATE) {
        // 候选者状态，拒绝客户端请求
        return "+TRYAGAIN\r\n";
    } else if (state == NodeState::FOLLOWER) {
        // 跟随者状态，重定向到Leader
        if (leader_id != 0) {
            return "+MOVED " + std::to_string(leader_id) + "\r\n";
        } else {
            return "+TRYAGAIN\r\n";
        }
    } else if (state == NodeState::LEADER) {
        std::string cmd_type = command[0];
        std::transform(cmd_type.begin(), cmd_type.end(), cmd_type.begin(), ::toupper);

        // 添加到日志
        int current_term = raft_core_->getCurrentTerm();
        int log_index = raft_core_->appendLogEntry(original_request, current_term);
        //std::cout<<"[RaftNode:] " <<current_term << " "<< log_index << std::endl;
        
        int commit_index = raft_core_->getCommitIndex();

        if (cmd_type == "DEL") {
        //等待log_index-1被提交 
            while (log_index-1 > commit_index) {
                //std::cout<<"[RaftNode:] " <<current_term << " "<< commit_index << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                commit_index = raft_core_->getCommitIndex();
            }
        }

        int del_count = 0;//不能等del应用到状态机再在统计，因为del删除完后再去统计，会导致del_count为0
        if (cmd_type == "DEL") {
            if (command.size() >= 2) {
                for (size_t i = 1; i < command.size(); ++i) {
                    if (!kv_store_->get(command[i]).empty()) {
                        del_count++;
                    }
                }
            }
        }
        // 等待日志被提交
        while (log_index > commit_index) {
            //std::cout<<"[RaftNode:] " <<current_term << " "<< commit_index << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            commit_index = raft_core_->getCommitIndex();
        }
        
        
        // 根据命令类型生成响应
        if (cmd_type == "GET") {
            if (command.size() >= 2) {
                std::string value = kv_store_->get(command[1]);
                if (value.empty()) {
                    // 返回nil值
                    return "*1\r\n$3\r\nnil\r\n";
                } else {
                    // 使用专门的GET响应编码方法
                    //td::cout<<"[RaftNode:] " << value << std::endl;
                    return RedisProtocol::encodeGetResponse(value);
                }
            }
            return RedisProtocol::encodeError("Wrong number of arguments for GET command");
        } else if (cmd_type == "SET") {
            if (command.size() >= 3) {
                return RedisProtocol::encodeStatus("OK");
            }
            return RedisProtocol::encodeError("Wrong number of arguments for SET command");
        } else if (cmd_type == "DEL") {
            if(command.size() >= 2){
                return RedisProtocol::encodeInteger(del_count);
            }
            return RedisProtocol::encodeError("Wrong number of arguments for DEL command");
        } else {
            return RedisProtocol::encodeError("Unknown command: " + command[0]);
        }
    }
    // 处理异常情况
    return RedisProtocol::encodeError("Internal server error");
}

// 处理命令应用到状态机
std::string RaftNode::applyCommand(const std::string& command) {
    // 解析命令
    std::vector<std::string> parsed = RedisProtocol::parseCommand(command);
    if (parsed.empty()) {
        return RedisProtocol::encodeError("Protocol error");
    }
    
    // 标准化命令
    std::string cmd_type = parsed[0];
    std::transform(cmd_type.begin(), cmd_type.end(), cmd_type.begin(), ::toupper);
    
    // 根据命令类型执行操作
    if (cmd_type == "GET") {
        // GET命令不会改变状态，仅查询
        return "";
    } else if (cmd_type == "SET" && parsed.size() >= 3) {
        // 设置键值
        std::string key = parsed[1];
        std::string value = parsed[2];
        // 如果有多个参数，合并为一个值
        for (size_t i = 3; i < parsed.size(); ++i) {
            value += " " + parsed[i];
        }
        kv_store_->set(key, value);
        return RedisProtocol::encodeStatus("OK");
    } else if (cmd_type == "DEL" && parsed.size() >= 2) {
        // 删除键
        int count = 0;
        for (size_t i = 1; i < parsed.size(); ++i) {
            if (!kv_store_->get(parsed[i]).empty()) {
                count++;
                kv_store_->del(parsed[i]);
            }
        }
        return RedisProtocol::encodeInteger(count);
    }
    
    return RedisProtocol::encodeError("unknown command");
}

// 日志应用线程主循环
void RaftNode::logApplierLoop() {
    std::cout << "LogApplier thread started" << std::endl;
    while (running_) {
        // 获取当前已提交但未应用的日志
        int last_applied = raft_core_->getLastApplied();
        int commit_index = raft_core_->getCommitIndex();
        
        if (last_applied < commit_index) {
            std::lock_guard<std::mutex> lock(apply_mutex_);
            // 逐条应用日志,当last_applied小于commit_index时，应用日志
            for (int i = last_applied + 1; i <= commit_index; ++i) {
                try {
                    std::string entry_data = log_store_->entry_at(i);
                    std::cout << "[RaftNode:] " << "Node(" << node_id_ << ")开始应用log(" << i << "): " << entry_data << std::endl;
                    
                    // 应用命令到状态机
                    applyCommand(entry_data);
                    
                    // 更新已应用索引
                    raft_core_->setLastApplied(i);
                    
                    std::cout << "[RaftNode:] " << "Node(" << node_id_ << ")完成应用log(" << i << ")" << std::endl;
                } catch (const std::exception& e) {
                    std::cerr << "[RaftNode:] " << "Node(" << node_id_ << ")应用日志失败: " << e.what() << std::endl;
                    break;
                }
            }
        }
        
        // 短暂休眠，避免CPU占用过高
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::cout << "LogApplier thread stopped" << std::endl;
}

} // namespace raft 