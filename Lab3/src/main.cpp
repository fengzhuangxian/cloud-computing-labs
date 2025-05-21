#include "core/raft_node.h"
#include <iostream>
#include <string>
#include <signal.h>
#include <unistd.h>
#include <thread>
#include <atomic>

using namespace raft;

std::atomic<bool> running(true);

void signal_handler(int signum) {
    std::cout << "Caught signal " << signum << ". Shutting down..." << std::endl;
    running = false;
}

int main(int argc, char* argv[]) {
    std::string config_path;
    if (argc != 3 || std::string(argv[1]) != "--config_path") {
        std::cerr << "用法: " << argv[0] << " --config_path <config_file>" << std::endl;
        return 1;
    }
    config_path = argv[2];
    std::string log_dir = "./log";
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    try {
        std::cout << "Starting Raft node with config " << config_path << std::endl;
        RaftNode node(config_path, log_dir);
        node.Run();
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::cout << "Stopping Raft node..." << std::endl;
        node.Stop();
        std::cout << "Raft node stopped cleanly" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
} 