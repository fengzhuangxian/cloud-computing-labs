#include "raft_core.h"
#include <chrono>
#include <iostream>
#include <sstream>
#include <random>

#include "../utils/tools.h"
#include "../include/constants.h"

namespace raft {

// 构造函数
RaftCore::RaftCore(int node_id, int cluster_size, LogStore* log_store, KVStore* kv_store)
    : id_(node_id), 
      cluster_size_(cluster_size), 
      log_store_(log_store), 
      kv_store_(kv_store),
      state_(NodeState::FOLLOWER),
      current_term_(0),
      voted_(false),
      vote_count_(0),
      leader_id_(0),
      received_heartbeat_(false),
      commit_index_(0),
      last_applied_(0),
      leader_commit_index_(0),
      ack_(0),
      response_node_count_(0),
      seq_(0),
      live_count_(0),
      running_(false) {
    
    // 初始化Leader状态数据
    match_index_.resize(cluster_size_ - 1, 0);
    match_term_.resize(cluster_size_ - 1, 0);
}

// 析构函数
RaftCore::~RaftCore() {
    // 确保停止所有线程
    stop();
}

// 启动Raft服务
void RaftCore::start() {
    if (running_) {
        return;  // 已经在运行了
    }
    
    running_ = true;
    
    // 启动主循环线程
    main_loop_thread_ = std::thread(&RaftCore::mainLoop, this);
    
    // 不再启动日志应用线程，该功能已移至RaftNode
    // log_applier_thread_ = std::thread(&RaftCore::logApplierLoop, this);
    
    std::cout <<"[RaftCore:] " << "Node " << id_ << " started as follower, term: " << current_term_ << std::endl;
}

// 停止Raft服务
void RaftCore::stop() {
    if (!running_) {
        return;  // 已经停止了
    }
    
    running_ = false;
    
    // 唤醒可能在等待的条件变量
    {
        std::lock_guard<std::mutex> lock(log_apply_mutex_);
        log_apply_cv_.notify_all();
    }
    
    // 等待主循环线程结束
    if (main_loop_thread_.joinable()) {
        main_loop_thread_.join();
    }
    
    // 不再需要等待日志应用线程，该功能已移至RaftNode
    // if (log_applier_thread_.joinable()) {
    //     log_applier_thread_.join();
    // }
    
    std::cout <<"[RaftCore:] " << "Node " << id_ << " stopped" << std::endl;
}

// 主循环
void RaftCore::mainLoop() {
    while (running_) {
        switch (state_) {
            case NodeState::FOLLOWER:
                followerLoop();
                break;
                
            case NodeState::CANDIDATE:
                candidateLoop();
                break;
                
            case NodeState::LEADER:
                leaderLoop();
                break;
        }
    }
}

// 处理接收到的消息
std::unique_ptr<Message> RaftCore::handleMessage(int from_node_id, const Message& message) {
    switch (message.getType()) {
        case MessageType::REQUESTVOTE_REQUEST: {
            const auto& request = static_cast<const RequestVoteRequest&>(message);
            return handleRequestVote(from_node_id, request);
        }
            
        case MessageType::REQUESTVOTE_RESPONSE: {
            const auto& response = static_cast<const RequestVoteResponse&>(message);
            handleRequestVoteResponse(from_node_id, response);
            return nullptr;
        }
            
        case MessageType::APPENDENTRIES_REQUEST: {
            const auto& request = static_cast<const AppendEntriesRequest&>(message);
            return handleAppendEntries(from_node_id, request);
        }
            
        case MessageType::APPENDENTRIES_RESPONSE: {
            const auto& response = static_cast<const AppendEntriesResponse&>(message);
            handleAppendEntriesResponse(from_node_id, response);
            return nullptr;
        }
            
        default://理论不会到这一步
            std::cerr << "[RaftCore:] " << "Unknown message type: " << static_cast<int>(message.getType()) << std::endl;
            return nullptr;
    }
}

// 添加日志条目
int RaftCore::appendLogEntry(const std::string& command, int term) {
    log_store_->append(command, term);
    return log_store_->latest_index();
}

// Follower状态循环
void RaftCore::followerLoop() {   
    while (running_ && state_ == NodeState::FOLLOWER) {
        // 获取随机选举超时时间
        int timeout = FOLLOWER_TIMEOUT_MS;
        // 等待超时时间
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout));
        // 检查是否收到心跳
        if (!received_heartbeat_) {
            // 未收到心跳，转换为候选者状态
            becomeCandidate();
        } else {
            // 收到了心跳，重置标志
            received_heartbeat_ = false;
        }
    }
}

// 获取其他节点ID列表
std::vector<int> RaftCore::getPeerNodeIds() const {
    std::vector<int> peers;
    // 根据集群大小和当前节点ID，计算其他节点的ID
    // 假设节点ID是连续的从1开始分配
    for (int id = 1; id <= cluster_size_; id++) {
        if (id != id_) {
            peers.push_back(id);
        }
    }
    return peers;
}

// 将节点ID转换为内部数组索引
int RaftCore::nodeIdToIndex(int node_id) const {
    // 确保node_id有效
    if (node_id <= 0 || node_id > cluster_size_) {
        std::cerr << "Invalid node ID: " << node_id << std::endl;
        return -1;
    }
    // 确保不是自己的ID
    if (node_id == id_) {
        std::cerr << "Trying to get index for self node ID" << std::endl;
        return -1;
    }
    // 计算节点ID在内部数组中的索引
    // match_index_和match_term_数组大小为cluster_size_-1，不包含自身
    // 我们需要将节点ID映射到[0, cluster_size_-2]范围内的索引
    int index = node_id - 1;  // 首先从1开始的ID映射到0开始的索引
    // 跳过自己的位置
    if (index >= id_ - 1) {
        index--;  // 如果索引大于等于自己的位置，减1跳过自己
    }
    
    return index;
}

// Candidate状态循环
void RaftCore::candidateLoop() {
    // 创建随机数生成器
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(ELECTION_TIMEOUT_MIN_MS, ELECTION_TIMEOUT_MAX_MS);
    
    while (running_ && state_ == NodeState::CANDIDATE) {
        // 设置已投票标志
        voted_ = true;  // 已经给自己投票
        // 开始新的选举
        current_term_++;//任期+1
        vote_count_ = 1;  // 先给自己投一票
        
        // 向其他节点发送投票请求
        std::vector<int> peer_ids = getPeerNodeIds();
        for (int peer_id : peer_ids) {
            std::cout<<"[RaftCore:] " << "向节点 " << peer_id << " 发送投票请求" << std::endl;
            sendRequestVote(peer_id);
        }
        
        // 等待随机的选举超时时间
        int timeout = dis(gen);
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout));
        
        // 检查状态
        if (state_ == NodeState::CANDIDATE) {
            // 选举超时，回到Follower状态
            becomeFollower(current_term_);
            std::cout << "[RaftCore:] " << id_ << " 未获得多数票，变回follower" << std::endl;
        }
        // 重置投票标志
        voted_ = false;
    }
}

// Leader状态循环
void RaftCore::leaderLoop() {
    while (running_ && state_ == NodeState::LEADER) {
        // 发送心跳
        std::vector<int> peer_ids = getPeerNodeIds();
        seq_=seq_==10?0:seq_+1;//seq_是心跳序列号，每10次心跳后重置为0
        for (int peer_id : peer_ids) {
            sendAppendEntries(peer_id, true);
        }
        
        // 等待心跳间隔
        std::this_thread::sleep_for(std::chrono::milliseconds(HEARTBEAT_INTERVAL_MS));
        
        // 检查Leader的存活计数
        live_count_--;
        if (live_count_ < 0) {
            // 长时间未收到大多数节点的响应，怀疑网络分区，退回到Follower状态
            becomeFollower(current_term_);
            std::cout <<"[RaftCore:] " << id_ << " 出现网络链接问题，退回到follower" << std::endl;
            break;
        }
    }
}

// 成为Follower
void RaftCore::becomeFollower(int term) {
    // 更新状态和任期
    state_ = NodeState::FOLLOWER;
    current_term_ = term;
    leader_id_ = 0;  // 未知的领导者
    
    // 重置其他状态
    voted_ = false;
    vote_count_ = 0;
    received_heartbeat_ = false;
}


// 成为Candidate
void RaftCore::becomeCandidate() {
    // 更新状态
    state_ = NodeState::CANDIDATE;
    std::cout <<"[RaftCore:] " << id_ << " become candidate, term= " << current_term_ + 1 << std::endl;
}

// 成为Leader
void RaftCore::becomeLeader() {
    // 更新状态
    state_ = NodeState::LEADER;
    leader_id_ = id_;
    seq_ = 0;
    live_count_ = LEADER_RESILIENCE_COUNT;
    
    // 初始化Leader状态数据
    // 在这里加锁是因为match_index_和match_term_是共享资源
    // 因此需要互斥锁来保护这些共享资源,防止数据竞争
    std::lock_guard<std::mutex> lock(match_mutex_);//加锁
    
    // 获取最新日志信息
    int latest_term = log_store_->latest_term();
    // 获取最新日志索引
    int latest_index = log_store_->latest_index();
    for (int i = 0; i < cluster_size_ - 1; ++i) {
        match_index_[i] = latest_index;
        match_term_[i] = latest_term;
    }
    std::cout <<"[RaftCore:] " << id_ << " become leader, term=" << current_term_ << std::endl;
    
}



// 处理RequestVote请求
// 投票条件：
std::unique_ptr<Message> RaftCore::handleRequestVote(int from_node_id, const RequestVoteRequest& request) {
    // from_node_id参数未使用，但保留接口一致性
    //(void)from_node_id;
    std::cout<<"[RaftCore:] " << id_ << " 收到来自节点 " << from_node_id << " 的投票请求" << std::endl;
    auto response = std::make_unique<RequestVoteResponse>();
    //Job1:收到来自其他节点的投票请求，补全代码，构造回复给请求者的回应信息





    



    

    return response;
}



// 处理RequestVote响应
void RaftCore::handleRequestVoteResponse(int from_node_id, const RequestVoteResponse& response) {
    // from_node_id参数未使用，但保留接口一致性
    //(void)from_node_id;
    std::cout<<"[RaftCore:] " << id_ << " 收到来自节点 " << from_node_id << " 的投票响应" << std::endl;
    // 只有在candidate状态才处理投票响应
    if (state_ != NodeState::CANDIDATE) {
        return;
    }
    //Job2:收到来自其他节点的投票回应，补全代码，做出对应的反应









}

// 处理AppendEntries请求
std::unique_ptr<Message> RaftCore::handleAppendEntries(int from_node_id, const AppendEntriesRequest& request) {
    // from_node_id参数未使用，但保留接口一致性
    //(void)from_node_id;
    //std::cout<<"[RaftCore:] " << id_ << " 收到来自节点 " << from_node_id << " 的日志同步请求" << std::endl;
    auto response = std::make_unique<AppendEntriesResponse>();
    //Job3:收到来自leader节点的日志同步请求（心跳），补全代码，构造正确的回应消息










    return response;
}

// 处理AppendEntries响应
void RaftCore::handleAppendEntriesResponse(int from_node_id, const AppendEntriesResponse& response) {
    // from_node_id参数未使用，但保留接口一致性
    //(void)from_node_id;
    // 只有在Leader状态下才处理AppendEntries响应
    if (state_ != NodeState::LEADER) {
        return;
    }
    //Job4:收到来自follower节点的日志同步回应，补全代码，做出正确的反应










    
    
}



// 发送RequestVote请求
void RaftCore::sendRequestVote(int target_id) {
    if (target_id == id_) {
        return;  // 不向自己发送
    }
    // 创建RequestVote请求
    auto request = std::make_unique<RequestVoteRequest>();
    request->term = current_term_;
    request->candidate_id = id_;
    
    // 获取最后一条日志的索引和任期
    int last_log_index = log_store_->latest_index();
    int last_log_term = log_store_->latest_term();
    
    request->last_log_index = last_log_index;
    request->last_log_term = last_log_term;
    
    // 发送请求
    sendMessage(target_id, *request);
}


// 发送AppendEntries请求
void RaftCore::sendAppendEntries(int target_id, bool is_heartbeat) {
    if (target_id == id_) {
        return;  // 不向自己发送
    }

    // 创建AppendEntries请求
    auto request = std::make_unique<AppendEntriesRequest>();
    request->term = current_term_;
    request->leader_id = id_;
    request->seq = seq_;
    
    // 获取目标节点的下一个日志索引
    int prev_log_index = 0;
    int prev_log_term = 0;
    {
        std::lock_guard<std::mutex> lock(match_mutex_);
        
        int idx = nodeIdToIndex(target_id);
        if (idx >= 0 && idx < static_cast<int>(match_index_.size())) {
            prev_log_index = match_index_[idx];
        } else {
            std::cout << "Sending message to node " << target_id << std::endl;
            std::cerr << "Unknown target node ID: " << target_id << std::endl;
            return;
        }
    }
    
    // 获取前一个日志的任期
    if (prev_log_index > 0) {
        prev_log_term = log_store_->term_at(prev_log_index);
    }
    
    request->prev_log_index = prev_log_index;
    request->prev_log_term = prev_log_term;
    request->leader_commit = commit_index_;
    
    // 如果是心跳，则不附加日志条目
    if (is_heartbeat) {
        // 添加日志条目
        int next_index = prev_log_index + 1;
        int last_index = log_store_->latest_index();
        
        for (int i = next_index; i <= last_index && i <= next_index + BATCH_SIZE; ++i) {
            LogEntry entry;
            entry.term = log_store_->term_at(i);
            entry.data = log_store_->entry_at(i);
            request->entries.push_back(entry);
        }
    }
    
    // 发送请求
    sendMessage(target_id, *request);
}

// 发送消息
bool RaftCore::sendMessage(int target_id, const Message& message) {
    if (send_message_callback_) {
        return send_message_callback_(target_id, message);
    }
    return false;
}

} // namespace raft

