#include "raft_core.h"
#include <chrono>
#include <iostream>
#include <sstream>
#include <random>

#include "../utils/tools.h"
#include "../include/constants.h"
#include <algorithm>

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

// 主循环(一个线程)
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

    // 设置响应中的任期为当前任期
    response->term = current_term_;
    response->vote_granted = false;// 默认不投票
    

    //加粗粒度的锁，将std::lock_guard<std::mutex> lock(vote_mutex_);移到这里
    std::lock_guard<std::mutex> lock(vote_mutex_);
    //节点状态，和voted都是原子变量，但还是要加锁，保证一次只处理一个消息，避免并发问题
    //该锁保护的变量有，节点状态，voted_, current_term_

    //仅在Follower状态下且没有投票处理投票请求
    //TODU:如果是Candidate和LEADER状态，就一定不处理直接返回不投票吗，如果投票请求的任期比当前任期高呢
    if(state_==NodeState::FOLLOWER){
        // 1. 请求中的任期大于等于当前任期
        // 2. 请求中的日志至少与当前节点一样新
        // 3. 节点还未投票给其他节点

        //加锁,防止多个线程在vote_=flase时同时给多个节点投票
        //给vote_mutex_加锁，保护voted_变量状态
        //std::lock_guard<std::mutex> lock(vote_mutex_);
        //已移到前面

        //未投票且当前无leader时才可能投票
        if(!voted_){

            // 获取自己最后一条日志的信息，包括索引和任期
            int last_log_index = log_store_->latest_index();
            int last_log_term = log_store_->latest_term();

            int vote_term_flag=0;//确认任期更新
            int vote_log_flag=0;//确认日志至少一样新

            if(request.term >= current_term_) {
                vote_term_flag=1;
                //current_term_=request.term;
            }

            //如果请求的最新日志任期大于当前节点的日志任期，则认为请求的日志更加新
            if(request.last_log_term > last_log_term) {
                vote_log_flag=1;
            }

            //如果请求的最新日志任期与当前节点的日志任期相同，且请求的最新日志索引大于等于当前节点的日志索引，则认为请求的日志更加新
            if(request.last_log_term == last_log_term && request.last_log_index >= last_log_index) {
                vote_log_flag=1;
            }

            //如果任期和日志都满足条件，则授予投票
            if (vote_term_flag==1 && vote_log_flag==1) {
                current_term_ = request.term; // 更新当前任期
                leader_id_ = request.candidate_id; // 更新领导者ID
                //构造投票信息
                response->term = current_term_;
                response->vote_granted = true;
                voted_ = true;
                std::cout<<"[RaftCore:] " << id_ << " 投票给节点 " << from_node_id  << std::endl;
            }

            // 重置心跳接收标志，避免立即触发新选举
            // 这是因为followerloop中会检查received_heartbeat_来决定是否转换为候选者状态
            // 收到投票请求的情况下，只有在follower状态且没投过票才表示自己收到心跳
            // 若是Candidate和leader，则没有心跳变量，若是follower但已投过票，则接收心跳来重置这个变量
            received_heartbeat_ = true;
        }

    }
    //TODU:如果是Candidate和LEADER状态，就一定不处理直接返回不投票吗，如果投票请求的任期比当前任期高呢

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

    // 加锁保护投票相关状态
    std::lock_guard<std::mutex> lock(vote_mutex_);

    // 如果收到的响应中任期大于当前任期，转为Follower
    if (response.term > current_term_) {
        becomeFollower(response.term);
        return;
    }
    
    // 如果收到投票
    if (response.vote_granted) {
        vote_count_++;
        std::cout<<"[RaftCore:] " << id_ << " 当前获得票数: " << vote_count_ << std::endl;
        
        // 如果获得多数票，成为Leader
        if (vote_count_ > cluster_size_ / 2) {
            std::cout<<"[RaftCore:] " << id_ << " 获得多数票，成为leader" << std::endl;
            becomeLeader();
        }
    }

    return;
}

// 处理AppendEntries请求
std::unique_ptr<Message> RaftCore::handleAppendEntries(int from_node_id, const AppendEntriesRequest& request) {
    // from_node_id参数未使用，但保留接口一致性
    //(void)from_node_id;
    //std::cout<<"[RaftCore:] " << id_ << " 收到来自节点 " << from_node_id << " 的日志同步请求" << std::endl;
    auto response = std::make_unique<AppendEntriesResponse>();
    //Job3:收到来自leader节点的日志同步请求（心跳），补全代码，构造正确的回应消息

    // 设置默认响应(未添加成功)
    response->term = current_term_;
    response->follower_id = id_;
    response->log_index = log_store_->latest_index();
    response->follower_commit = commit_index_;
    response->ack = request.seq;
    response->success = false;
    
    // 如果请求中的任期小于当前任期，拒绝请求
    if (request.term < current_term_) {
        return response;
    }
    
    // 收到来自合法leader的心跳，重置接收标志
    received_heartbeat_ = true;
    
    // 如果请求中的任期大于当前任期，转为Follower
    if (request.term > current_term_) {
        becomeFollower(request.term);
    }
    
    // 检查日志一致性
    if (request.prev_log_index > 0) {
        // 如果前一个日志索引大于本地最新日志索引，或者任期不匹配，拒绝请求
        if (request.prev_log_index > log_store_->latest_index() ||
            log_store_->term_at(request.prev_log_index) != request.prev_log_term) {
            return response;
        }
    }

    std::lock_guard<std::mutex> lock(log_apply_mutex_);
    
    // 追加新的日志条目
    for (size_t i = 0; i < request.entries.size(); i++) {
        int current_index = request.prev_log_index + 1 + i;
        // 如果当前索引的日志已存在且任期不同，删除从这个索引开始的所有日志
        // if (current_index <= log_store_->latest_index() &&
        //     log_store_->term_at(current_index) != request.entries[i].term) {
        //     删除不一致的日志及其之后的所有日志
        //     while (log_store_->latest_index() >= current_index) {
        //         log_store_->pop_back();
        //     }
        // }
        // 如果是新的日志条目，追加到日志中
        if (current_index > log_store_->latest_index()) {
            log_store_->append(request.entries[i].data, request.entries[i].term);
        }
    }
    
    // 更新提交索引
    if (request.leader_commit > commit_index_) {
        commit_index_ = std::min(request.leader_commit, log_store_->latest_index());
    }
    
    // 设置响应成功
    response->log_index = log_store_->latest_index();
    response->follower_commit = commit_index_;
    response->success = true;
    
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

    // 如果收到的响应中任期大于当前任期，转为Follower
    if (response.term > current_term_) {
        becomeFollower(response.term);
        return;
    }
    
    // 更新存活计数
    if (response.success) {
        live_count_ = LEADER_RESILIENCE_COUNT;
    }
    
    // 如果是旧的响应，忽略
    if (response.ack != seq_) {
        return;
    }
    
    // 处理成功的响应
    if (response.success) {
        // 使用互斥锁保护match_index_和match_term_的更新
        std::lock_guard<std::mutex> lock(match_mutex_);
        
        int node_idx = nodeIdToIndex(response.follower_id);
        if (node_idx >= 0 && node_idx < static_cast<int>(match_index_.size())) {
            // 更新该节点的日志匹配信息
            match_index_[node_idx] = response.log_index;
            match_term_[node_idx] = log_store_->term_at(response.log_index);
            
            // 计算可以提交的新索引
            std::vector<int> sorted_indices = match_index_;
            // 对match_index_进行排序，选取中位数提交
            std::sort(sorted_indices.begin(), sorted_indices.end());
            int majority_index = sorted_indices[(cluster_size_ - 1) / 2];
            
            // 如果多数节点已复制且任期正确，更新提交索引
            if (majority_index > commit_index_ && 
                log_store_->term_at(majority_index) == current_term_) {
                commit_index_ = majority_index;
            }
        }
    } else {
        // 如果失败，在下一次心跳时会自动重试
        // 这里可以选择立即重试，但为了避免网络负载，我们等待下一次心跳
    }
    
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
        // 使用互斥锁保护match_index_的访问
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

