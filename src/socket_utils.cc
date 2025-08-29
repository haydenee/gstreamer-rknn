#include "socket_utils.h"
#include <iostream>
#include <errno.h>
#include "nlohmann/json.hpp"

// ==================== SocketUtils 实现 ====================

bool SocketUtils::send_message(int sockfd, const std::string& message) {
    // 格式化消息：长度 + \n + 内容
    std::string formatted_message = std::to_string(message.size()) + "\n" + message;
    
    ssize_t total_sent = 0;
    ssize_t bytes_sent;
    const char* data = formatted_message.c_str();
    ssize_t total_length = formatted_message.size();
    
    while (total_sent < total_length) {
        bytes_sent = write(sockfd, data + total_sent, total_length - total_sent);
        if (bytes_sent <= 0) {
            if (bytes_sent == 0) {
                // 连接关闭
                return false;
            }
            if (errno == EINTR) {
                // 被信号中断，继续尝试
                continue;
            }
            // 其他错误
            return false;
        }
        total_sent += bytes_sent;
    }
    
    return true;
}

bool SocketUtils::receive_message(int sockfd, std::string& message) {
    // 先读取长度行
    std::string length_str;
    char ch;
    ssize_t bytes_read;
    
    while (true) {
        bytes_read = read(sockfd, &ch, 1);
        if (bytes_read <= 0) {
            if (bytes_read == 0) {
                // 连接关闭
                return false;
            }
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        
        if (ch == '\n') {
            break;
        }
        
        length_str += ch;
    }
    
    // 转换长度
    int length;
    try {
        length = std::stoi(length_str);
    } catch (const std::exception& e) {
        return false;
    }
    
    if (length <= 0) {
        return false;
    }
    
    // 读取消息内容
    message.resize(length);
    ssize_t total_read = 0;
    while (total_read < length) {
        bytes_read = read(sockfd, &message[total_read], length - total_read);
        if (bytes_read <= 0) {
            if (bytes_read == 0) {
                return false;
            }
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        total_read += bytes_read;
    }
    
    return true;
}

int SocketUtils::connect_to_server(const std::string& host, int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        return -1;
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
        close(sockfd);
        return -1;
    }
    
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(sockfd);
        return -1;
    }
    
    return sockfd;
}

int SocketUtils::create_server(int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        return -1;
    }
    
    // 设置SO_REUSEADDR选项
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close(sockfd);
        return -1;
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(sockfd);
        return -1;
    }
    
    if (listen(sockfd, 5) < 0) {
        close(sockfd);
        return -1;
    }
    
    return sockfd;
}

// ==================== SocketClient 实现 ====================

SocketClient::SocketClient(const std::string& hostname) 
    : sockfd_(-1), hostname_(hostname), primary_port_(-1), connected_(false) {
}

SocketClient::~SocketClient() {
    disconnect();
}

bool SocketClient::connect(const std::string& primary_host, int primary_port) {
    if (connected_) {
        disconnect();
    }
    
    sockfd_ = SocketUtils::connect_to_server(primary_host, primary_port);
    if (sockfd_ < 0) {
        return false;
    }
    
    // 发送hostname作为第一条消息
    if (!SocketUtils::send_message(sockfd_, hostname_)) {
        close(sockfd_);
        sockfd_ = -1;
        return false;
    }
    
    primary_host_ = primary_host;
    primary_port_ = primary_port;
    connected_ = true;
    
    return true;
}

bool SocketClient::send_detection(RknnMeta* meta) {
    if (!connected_) {
        return false;
    }
    
    // 创建 JSON 对象
    nlohmann::json detection_json;
    
    // 创建 annots 数组
    nlohmann::json annots_array = nlohmann::json::array();
    
    // 遍历所有检测结果
    for (int i = 0; i < meta->results_size; i++) {
        nlohmann::json annot_obj;
        
        // 设置 personID
        annot_obj["personID"] = i;
        
        // 创建 bbox 数组 [x, y, w, h, conf]
        nlohmann::json bbox_array = nlohmann::json::array();
        bbox_array.push_back(meta->results[i][0]);  // x
        bbox_array.push_back(meta->results[i][1]);  // y
        bbox_array.push_back(meta->results[i][2]);  // w
        bbox_array.push_back(meta->results[i][3]);  // h
        bbox_array.push_back(meta->results[i][4]);  // conf
        annot_obj["bbox"] = bbox_array;
        
        // 创建 keypoints 数组，包含17个关键点
        nlohmann::json keypoints_array = nlohmann::json::array();
        for (int j = 0; j < 17; j++) {
            nlohmann::json keypoint = nlohmann::json::array();
            // 每个关键点包含 x, y, conf
            // 在 results 数组中，关键点数据从索引5开始
            int base_idx = 5 + j * 3;
            keypoint.push_back(meta->results[i][base_idx]);      // x
            keypoint.push_back(meta->results[i][base_idx + 1]);  // y
            keypoint.push_back(meta->results[i][base_idx + 2]);  // conf
            keypoints_array.push_back(keypoint);
        }
        annot_obj["keypoints"] = keypoints_array;
        
        annots_array.push_back(annot_obj);
    }
    
    // 设置 annots 数组
    detection_json["annots"] = annots_array;
    
    // 设置 cameraName 为当前客户端的 hostname
    detection_json["cameraName"] = hostname_;
    
    // 设置 height 和 width
    detection_json["height"] = meta->height;
    detection_json["width"] = meta->width;
    
    // 将 JSON 对象转换为字符串
    std::string json_str = detection_json.dump();
    
    // 发送 JSON 消息
    return SocketUtils::send_message(sockfd_, json_str);
}

void SocketClient::disconnect() {
    if (sockfd_ >= 0) {
        close(sockfd_);
        sockfd_ = -1;
    }
    connected_ = false;
}

// ==================== SocketPrimary 实现 ====================

SocketPrimary::SocketPrimary(const std::string& register_name)
    : server_sockfd_(-1), listen_sockfd_(-1), register_name_(register_name),
      server_port_(-1), client_port_(-1), connected_to_server_(false),
      stop_sender_(false) {
}

SocketPrimary::~SocketPrimary() {
    stop_sender_thread();
    disconnect_from_server();
    if (listen_sockfd_ >= 0) {
        close(listen_sockfd_);
        listen_sockfd_ = -1;
    }
}

bool SocketPrimary::connect_to_server(const std::string& server_host, int server_port) {
    if (connected_to_server_) {
        disconnect_from_server();
    }
    
    server_sockfd_ = SocketUtils::connect_to_server(server_host, server_port);
    if (server_sockfd_ < 0) {
        return false;
    }
    
    // 发送register_name作为第一条消息
    if (!SocketUtils::send_message(server_sockfd_, register_name_)) {
        close(server_sockfd_);
        server_sockfd_ = -1;
        return false;
    }
    
    server_host_ = server_host;
    server_port_ = server_port;
    connected_to_server_ = true;
    
    return true;
}

bool SocketPrimary::start_listening(int client_port) {
    if (listen_sockfd_ >= 0) {
        close(listen_sockfd_);
        listen_sockfd_ = -1;
    }
    
    listen_sockfd_ = SocketUtils::create_server(client_port);
    if (listen_sockfd_ < 0) {
        return false;
    }
    
    client_port_ = client_port;
    return true;
}

bool SocketPrimary::accept_client(int& client_sockfd, std::string& client_hostname) {
    if (listen_sockfd_ < 0) {
        return false;
    }
    
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    client_sockfd = accept(listen_sockfd_, (struct sockaddr*)&client_addr, &client_len);
    if (client_sockfd < 0) {
        return false;
    }
    
    // 接收client的hostname作为第一条消息
    if (!SocketUtils::receive_message(client_sockfd, client_hostname)) {
        close(client_sockfd);
        return false;
    }
    
    return true;
}

bool SocketPrimary::send_to_server(const std::string& aggregated_info) {
    if (!connected_to_server_) {
        return false;
    }
    
    return SocketUtils::send_message(server_sockfd_, aggregated_info);
}

void SocketPrimary::disconnect_from_server() {
    if (server_sockfd_ >= 0) {
        close(server_sockfd_);
        server_sockfd_ = -1;
    }
    connected_to_server_ = false;
}

bool SocketPrimary::receive_client_message(int client_sockfd, const std::string& client_hostname) {
    std::string message;
    if (!SocketUtils::receive_message(client_sockfd, message)) {
        return false;
    }
    
    {
        std::lock_guard<std::mutex> lock(messages_mutex_);
        
        // 如果是新的客户端，添加到顺序列表中
        if (client_messages_.find(client_hostname) == client_messages_.end()) {
            client_order_.push_back(client_hostname);
        }
        
        // 更新客户端的最新消息
        client_messages_[client_hostname] = message;
    }
    
    return true;
}

void SocketPrimary::start_sender_thread() {
    if (sender_thread_.joinable()) {
        return; // 线程已经在运行
    }
    
    stop_sender_ = false;
    sender_thread_ = std::thread(&SocketPrimary::sender_thread_func, this);
}

void SocketPrimary::stop_sender_thread() {
    if (!sender_thread_.joinable()) {
        return; // 线程未运行
    }
    
    {
        std::lock_guard<std::mutex> lock(messages_mutex_);
        stop_sender_ = true;
    }
    
    sender_cv_.notify_all();
    sender_thread_.join();
}

void SocketPrimary::sender_thread_func() {
    // 计算30fps的间隔（约33.33毫秒）
    const auto frame_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(1.0 / 30.0));
    
    // 获取第一个时间点
    auto next_send_time = std::chrono::steady_clock::now();
    
    while (true) {
        // 检查是否应该停止线程
        {
            std::lock_guard<std::mutex> lock(messages_mutex_);
            if (stop_sender_) {
                break;
            }
        }
        
        // 获取所有客户端的最新消息
        std::vector<std::pair<std::string, std::string>> messages_to_send;
        {
            std::lock_guard<std::mutex> lock(messages_mutex_);
            
            // 按照客户端连接顺序获取消息
            for (const auto& hostname : client_order_) {
                auto it = client_messages_.find(hostname);
                if (it != client_messages_.end()) {
                    messages_to_send.emplace_back(hostname, it->second);
                }
            }
        }
        
        // 发送每个客户端的最新消息到server
        for (const auto& pair : messages_to_send) {
            if (!send_to_server(pair.second)) {
                // 发送失败，记录错误或处理
                std::cerr << "Failed to send message from client " << pair.first << " to server" << std::endl;
            }
        }
        
        // 计算下一次发送的时间点
        next_send_time += frame_interval;
        
        // 等待直到下一次应该发送的时间点
        std::unique_lock<std::mutex> lock(messages_mutex_);
        sender_cv_.wait_until(lock, next_send_time, [this] { return stop_sender_; });
    }
}