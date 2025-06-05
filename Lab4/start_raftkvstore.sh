#!/bin/bash
# 使用该脚本前要确保已经编译了 kvstoreraftsystem

# 定义要启动的 Raft 节点命令
RAFT_NODES=(
    "../Lab3/kvstoreraftsystem --config_path ../Lab3/conf/1.conf"
    "../Lab3/kvstoreraftsystem --config_path ../Lab3/conf/2.conf"
    "../Lab3/kvstoreraftsystem --config_path ../Lab3/conf/3.conf"
)

# 存储后台进程的 PID
PIDS=()

# 启动所有 Raft 节点（后台运行）
for cmd in "${RAFT_NODES[@]}"; do
    $cmd &
    PIDS+=($!)  # 记录 PID
    echo "Started PID $! with command: $cmd"
done

# 捕获 Ctrl+C 信号，关闭所有进程
trap 'echo "Stopping all Raft nodes..."; kill ${PIDS[*]}' SIGINT

# 等待所有进程结束（或用户按 Ctrl+C）
wait ${PIDS[*]}
