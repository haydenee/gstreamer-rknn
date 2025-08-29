#include "socket_utils.h"
#include <iostream>
#include <fstream>
#include <errno.h>
#include "nlohmann/json.hpp"
#include <gst/gstinfo.h>

GST_DEBUG_CATEGORY_EXTERN(gst_plugin_rknn_debug);
#define GST_CAT_DEFAULT gst_plugin_rknn_debug

// ==================== SocketUtils 实现 ====================

bool SocketUtils::send_message(int sockfd, const std::string& message) {
    GST_DEBUG("Sending message, size: %zu bytes", message.size());
    
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
                GST_WARNING("Connection closed while sending message");
                return false;
            }
            if (errno == EINTR) {
                // 被信号中断，继续尝试
                GST_DEBUG("Write interrupted by signal, retrying");
                continue;
            }
            // 其他错误
            GST_ERROR("Failed to send message: %s (errno: %d)", strerror(errno), errno);
            return false;
        }
        total_sent += bytes_sent;
    }
    
    GST_DEBUG("Message sent successfully, total bytes: %zd", total_sent);
    return true;
}

bool SocketUtils::receive_message(int sockfd, std::string& message) {
    GST_DEBUG("Waiting to receive message");
    
    // 先读取长度行
    std::string length_str;
    char ch;
    ssize_t bytes_read;
    
    while (true) {
        bytes_read = read(sockfd, &ch, 1);
        if (bytes_read <= 0) {
            if (bytes_read == 0) {
                // 连接关闭
                GST_WARNING("Connection closed while receiving message length");
                return false;
            }
            if (errno == EINTR) {
                GST_DEBUG("Read interrupted by signal, retrying");
                continue;
            }
            GST_ERROR("Failed to receive message length: %s (errno: %d)", strerror(errno), errno);
            return false;
        }
        
        if (ch == '\n') {
            break;
        }
        
        length_str += ch;
    }
    
    GST_DEBUG("Received message length string: '%s'", length_str.c_str());
    
    // 转换长度
    int length;
    try {
        length = std::stoi(length_str);
    } catch (const std::exception& e) {
        GST_ERROR("Failed to parse message length '%s': %s", length_str.c_str(), e.what());
        return false;
    }
    
    if (length <= 0) {
        GST_ERROR("Invalid message length: %d", length);
        return false;
    }
    
    GST_DEBUG("Message length: %d bytes", length);
    
    // 读取消息内容
    message.resize(length);
    ssize_t total_read = 0;
    while (total_read < length) {
        bytes_read = read(sockfd, &message[total_read], length - total_read);
        if (bytes_read <= 0) {
            if (bytes_read == 0) {
                GST_WARNING("Connection closed while receiving message content");
                return false;
            }
            if (errno == EINTR) {
                GST_DEBUG("Read interrupted by signal, retrying");
                continue;
            }
            GST_ERROR("Failed to receive message content: %s (errno: %d)", strerror(errno), errno);
            return false;
        }
        total_read += bytes_read;
    }
    
    GST_DEBUG("Message received successfully, total bytes: %zd", total_read);
    return true;
}

int SocketUtils::connect_to_server(const std::string& host, int port) {
    GST_INFO("Connecting to server %s:%d", host.c_str(), port);
    
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        GST_ERROR("Failed to create socket: %s (errno: %d)", strerror(errno), errno);
        return -1;
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
        GST_ERROR("Failed to convert address %s: %s (errno: %d)", host.c_str(), strerror(errno), errno);
        close(sockfd);
        return -1;
    }
    
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        GST_ERROR("Failed to connect to %s:%d: %s (errno: %d)", host.c_str(), port, strerror(errno), errno);
        close(sockfd);
        return -1;
    }
    
    GST_INFO("Successfully connected to server %s:%d", host.c_str(), port);
    return sockfd;
}

int SocketUtils::create_server(int port) {
    GST_INFO("Creating server socket on port %d", port);
    
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        GST_ERROR("Failed to create server socket: %s (errno: %d)", strerror(errno), errno);
        return -1;
    }
    
    // 设置SO_REUSEADDR选项
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        GST_WARNING("Failed to set SO_REUSEADDR: %s (errno: %d)", strerror(errno), errno);
        // 继续执行，这不是致命错误
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        GST_ERROR("Failed to bind to port %d: %s (errno: %d)", port, strerror(errno), errno);
        close(sockfd);
        return -1;
    }
    
    if (listen(sockfd, 5) < 0) {
        GST_ERROR("Failed to listen on port %d: %s (errno: %d)", port, strerror(errno), errno);
        close(sockfd);
        return -1;
    }
    
    GST_INFO("Server socket created successfully on port %d", port);
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
    GST_INFO("SocketClient connecting to primary %s:%d", primary_host.c_str(), primary_port);
    
    if (connected_) {
        GST_DEBUG("Already connected, disconnecting first");
        disconnect();
    }
    
    sockfd_ = SocketUtils::connect_to_server(primary_host, primary_port);
    if (sockfd_ < 0) {
        GST_ERROR("Failed to connect to primary %s:%d", primary_host.c_str(), primary_port);
        return false;
    }
    
    GST_DEBUG("Connected to primary, sending hostname: %s", hostname_.c_str());
    
    // 发送hostname作为第一条消息
    if (!SocketUtils::send_message(sockfd_, hostname_)) {
        GST_ERROR("Failed to send hostname to primary");
        close(sockfd_);
        sockfd_ = -1;
        return false;
    }
    
    primary_host_ = primary_host;
    primary_port_ = primary_port;
    connected_ = true;
    
    GST_INFO("Successfully connected to primary %s:%d and sent hostname", primary_host.c_str(), primary_port);
    return true;
}

bool SocketClient::send_detection(RknnMeta* meta) {
    if (!connected_) {
        GST_WARNING("Cannot send detection: not connected to primary");
        return false;
    }
    
    GST_DEBUG("Preparing detection data for %d results", meta->results_size);
    
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
    
    GST_DEBUG("Sending detection data: %zu bytes, %d persons detected",
              json_str.size(), meta->results_size);
    
    // 发送 JSON 消息
    bool result = SocketUtils::send_message(sockfd_, json_str);
    if (result) {
        GST_DEBUG("Detection data sent successfully");
    } else {
        GST_ERROR("Failed to send detection data");
    }
    
    return result;
}

void SocketClient::disconnect() {
    if (sockfd_ >= 0) {
        GST_INFO("Disconnecting from primary %s:%d", primary_host_.c_str(), primary_port_);
        close(sockfd_);
        sockfd_ = -1;
    } else {
        GST_DEBUG("No active connection to disconnect");
    }
    connected_ = false;
}

// ==================== SocketPrimary 实现 ====================

SocketPrimary::SocketPrimary(const std::string& register_name, const std::string& config_path)
    : server_sockfd_(-1), listen_sockfd_(-1), register_name_(register_name),
      server_port_(-1), client_port_(-1), connected_to_server_(false),
      stop_sender_(false) {
    
    // 如果提供了配置文件路径，则加载配置文件中的客户端列表
    if (!config_path.empty()) {
        try {
            std::ifstream config_file(config_path);
            if (config_file.is_open()) {
                nlohmann::json config = nlohmann::json::parse(config_file);
                auto clients = config["clients"];
                for (const auto& client : clients) {
                    config_clients_.push_back(client.get<std::string>());
                }
                GST_INFO("Loaded %zu clients from config file", config_clients_.size());
            } else {
                GST_WARNING("Failed to open config file: %s", config_path.c_str());
            }
        } catch (const std::exception& e) {
            GST_ERROR("Failed to parse config file: %s", e.what());
        }
    }
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
    GST_INFO("SocketPrimary connecting to server %s:%d", server_host.c_str(), server_port);
    
    if (connected_to_server_) {
        GST_DEBUG("Already connected to server, disconnecting first");
        disconnect_from_server();
    }
    
    server_sockfd_ = SocketUtils::connect_to_server(server_host, server_port);
    if (server_sockfd_ < 0) {
        GST_ERROR("Failed to connect to server %s:%d", server_host.c_str(), server_port);
        return false;
    }
    
    GST_DEBUG("Connected to server, sending register name: %s", register_name_.c_str());
    
    // 发送register_name作为第一条消息
    if (!SocketUtils::send_message(server_sockfd_, register_name_)) {
        GST_ERROR("Failed to send register name to server");
        close(server_sockfd_);
        server_sockfd_ = -1;
        return false;
    }
    
    server_host_ = server_host;
    server_port_ = server_port;
    connected_to_server_ = true;
    
    GST_INFO("Successfully connected to server %s:%d and sent register name", server_host.c_str(), server_port);
    return true;
}

bool SocketPrimary::start_listening(int client_port) {
    GST_INFO("Starting to listen for client connections on port %d", client_port);
    
    if (listen_sockfd_ >= 0) {
        GST_DEBUG("Already listening, closing existing socket first");
        close(listen_sockfd_);
        listen_sockfd_ = -1;
    }
    
    listen_sockfd_ = SocketUtils::create_server(client_port);
    if (listen_sockfd_ < 0) {
        GST_ERROR("Failed to create server socket on port %d", client_port);
        return false;
    }
    
    client_port_ = client_port;
    GST_INFO("Successfully started listening on port %d", client_port);
    return true;
}

bool SocketPrimary::accept_client(int& client_sockfd, std::string& client_hostname) {
    if (listen_sockfd_ < 0) {
        GST_ERROR("Cannot accept client: not listening");
        return false;
    }
    
    GST_DEBUG("Waiting for client connection...");
    
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    client_sockfd = accept(listen_sockfd_, (struct sockaddr*)&client_addr, &client_len);
    if (client_sockfd < 0) {
        GST_ERROR("Failed to accept client connection: %s (errno: %d)", strerror(errno), errno);
        return false;
    }
    
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    GST_INFO("Accepted connection from client %s:%d", client_ip, ntohs(client_addr.sin_port));
    
    // 接收client的hostname作为第一条消息
    if (!SocketUtils::receive_message(client_sockfd, client_hostname)) {
        GST_ERROR("Failed to receive hostname from client");
        close(client_sockfd);
        return false;
    }
    
    GST_INFO("Received hostname from client: %s", client_hostname.c_str());
    return true;
}

bool SocketPrimary::send_to_server(const std::string& aggregated_info) {
    if (!connected_to_server_) {
        GST_WARNING("Cannot send to server: not connected");
        return false;
    }
    
    GST_DEBUG("Sending aggregated info to server: %zu bytes", aggregated_info.size());
    
    bool result = SocketUtils::send_message(server_sockfd_, aggregated_info);
    if (result) {
        GST_DEBUG("Aggregated info sent successfully to server");
    } else {
        GST_ERROR("Failed to send aggregated info to server");
    }
    
    return result;
}

void SocketPrimary::disconnect_from_server() {
    if (server_sockfd_ >= 0) {
        GST_INFO("Disconnecting from server %s:%d", server_host_.c_str(), server_port_);
        close(server_sockfd_);
        server_sockfd_ = -1;
    } else {
        GST_DEBUG("No active server connection to disconnect");
    }
    connected_to_server_ = false;
}

bool SocketPrimary::receive_client_message(int client_sockfd, const std::string& client_hostname) {
    GST_DEBUG("Receiving message from client: %s", client_hostname.c_str());
    
    std::string message;
    if (!SocketUtils::receive_message(client_sockfd, message)) {
        GST_ERROR("Failed to receive message from client %s", client_hostname.c_str());
        return false;
    }
    
    {
        std::lock_guard<std::mutex> lock(messages_mutex_);
        
        // 更新客户端的最新消息
        client_messages_[client_hostname] = message;
        GST_DEBUG("Updated message from client %s, size: %zu bytes", client_hostname.c_str(), message.size());
    }
    
    return true;
}

void SocketPrimary::start_sender_thread() {
    if (sender_thread_.joinable()) {
        GST_DEBUG("Sender thread already running");
        return; // 线程已经在运行
    }
    
    stop_sender_ = false;
    sender_thread_ = std::thread(&SocketPrimary::sender_thread_func, this);
    GST_INFO("Started sender thread for 30fps message sending");
}
void SocketPrimary::stop_sender_thread() {
    if (!sender_thread_.joinable()) {
        GST_DEBUG("Sender thread not running");
        return; // 线程未运行
    }
    
    {
        std::lock_guard<std::mutex> lock(messages_mutex_);
        stop_sender_ = true;
        GST_DEBUG("Stopping sender thread");
    }
    
    sender_cv_.notify_all();
    sender_thread_.join();
    GST_INFO("Sender thread stopped");
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
            
            // 按照配置文件中的客户端顺序获取消息
            for (const auto& hostname : config_clients_) {
                auto it = client_messages_.find(hostname);
                if (it != client_messages_.end()) {
                    messages_to_send.emplace_back(hostname, it->second);
                }
            }
        }
        
        // 检查消息数量是否足够（需要所有客户端都有消息）
        size_t expected_client_count;
        {
            std::lock_guard<std::mutex> lock(messages_mutex_);
            expected_client_count = config_clients_.size();
        }
        
        if (messages_to_send.size() < expected_client_count) {
            // 消息数量不够，不发送任何消息
            GST_DEBUG("Not enough messages to send (expected %zu, got %zu), skipping send",
                     expected_client_count, messages_to_send.size());
        } else {
            // 发送每个客户端的最新消息到server
            for (const auto& pair : messages_to_send) {
                if (!send_to_server(pair.second)) {
                    // 发送失败，记录错误或处理
                    GST_ERROR("Failed to send message from client %s to server", pair.first.c_str());
                }
            }
        }
        
        // 计算下一次发送的时间点
        next_send_time += frame_interval;
        
        // 等待直到下一次应该发送的时间点
        std::unique_lock<std::mutex> lock(messages_mutex_);
        sender_cv_.wait_until(lock, next_send_time, [this] { return stop_sender_; });
    }
}