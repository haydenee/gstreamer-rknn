#include "socket_utils.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <netdb.h>
#include <chrono>
#include <sstream>
#include <fstream>
#include <poll.h>

namespace SocketUtils {

// socket_client 实现
SocketClient::SocketClient(const std::string& hostname, const std::string& primary_host, int port)
    : hostname(hostname), primary_host(primary_host), primary_port(port), 
      sockfd(-1), connected(false), running(false) {
}

SocketClient::~SocketClient() {
    stop_auto_reconnect();
    disconnect();
}

bool SocketClient::connect_to_primary_internal(int timeout_ms) {
    if (connected) {
        return true;
    }
    
    if (!utils::connect_to_tcp_server(primary_host, primary_port, sockfd, timeout_ms)) {
        return false;
    }
    
    // 发送hostname作为第一条消息
    if (!protocol::send_encoded_message(sockfd, hostname)) {
        utils::close_socket(sockfd);
        sockfd = -1;
        return false;
    }
    
    connected = true;
    return true;
}

bool SocketClient::connect_to_primary(int timeout_ms) {
    return connect_to_primary_internal(timeout_ms);
}

bool SocketClient::send_message(const std::string& message) {
    if (!connected) {
        return false;
    }
    
    return protocol::send_encoded_message(sockfd, message);
}

bool SocketClient::disconnect() {
    if (sockfd != -1) {
        utils::close_socket(sockfd);
        sockfd = -1;
    }
    connected = false;
    return true;
}

bool SocketClient::is_connected() const {
    return connected;
}

bool SocketClient::reconnect(int max_attempts, int base_delay_ms) {
    disconnect();
    
    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        if (connect_to_primary_internal(5000)) {
            return true;
        }
        
        if (attempt < max_attempts) {
            int delay_ms = base_delay_ms * (1 << (attempt - 1));
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
    }
    
    return false;
}

void SocketClient::reconnect_worker(int max_attempts, int base_delay_ms) {
    while (running) {
        if (!connected) {
            reconnect(max_attempts, base_delay_ms);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

void SocketClient::start_auto_reconnect(int max_attempts, int base_delay_ms) {
    if (running) {
        return;
    }
    
    running = true;
    reconnect_thread = std::thread(&SocketClient::reconnect_worker, this, max_attempts, base_delay_ms);
}

void SocketClient::stop_auto_reconnect() {
    if (!running) {
        return;
    }
    
    running = false;
    if (reconnect_thread.joinable()) {
        reconnect_thread.join();
    }
}

// socket_primary 实现
SocketPrimary::SocketPrimary(const std::string& hostname, int port)
    : hostname(hostname), listen_port(port), server_sockfd(-1), listening(false) {
}

SocketPrimary::~SocketPrimary() {
    stop_listening();
}

bool SocketPrimary::start_listening(int backlog) {
    if (listening) {
        return true;
    }
    
    if (!utils::create_tcp_server(listen_port, server_sockfd, backlog)) {
        return false;
    }
    
    listening = true;
    accept_thread = std::thread(&SocketPrimary::accept_connections_worker, this);
    
    return true;
}

void SocketPrimary::stop_listening() {
    if (!listening) {
        return;
    }
    
    listening = false;
    
    if (server_sockfd != -1) {
        utils::close_socket(server_sockfd);
        server_sockfd = -1;
    }
    
    if (accept_thread.joinable()) {
        accept_thread.join();
    }
    
    if (server_thread.joinable()) {
        server_thread.join();
    }
    
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (auto& client : client_connections) {
        utils::close_socket(client.first);
    }
    client_connections.clear();
}

void SocketPrimary::accept_connections_worker() {
    while (listening) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_sockfd = accept(server_sockfd, (struct sockaddr*)&client_addr, &client_len);
        if (client_sockfd == -1) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            break;
        }
        
        // 接收hostname作为第一条消息
        std::string hostname = protocol::receive_encoded_message(client_sockfd, 5000);
        if (!hostname.empty()) {
            std::lock_guard<std::mutex> lock(clients_mutex);
            client_connections[client_sockfd] = hostname;
        } else {
            utils::close_socket(client_sockfd);
        }
    }
}

std::string SocketPrimary::collect_messages() {
    std::string aggregated_messages;
    std::vector<int> disconnected_clients;
    
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        for (auto& client : client_connections) {
            int client_sockfd = client.first;
            std::string client_hostname = client.second;
            
            std::string message = protocol::receive_encoded_message(client_sockfd, 100);
            if (!message.empty()) {
                if (!aggregated_messages.empty()) {
                    aggregated_messages += "\n";
                }
                aggregated_messages += "[" + client_hostname + "] " + message;
            } else {
                disconnected_clients.push_back(client_sockfd);
            }
        }
    }
    
    // 清理断开连接的客户端
    for (int sockfd : disconnected_clients) {
        std::lock_guard<std::mutex> lock(clients_mutex);
        auto it = client_connections.find(sockfd);
        if (it != client_connections.end()) {
            utils::close_socket(sockfd);
            client_connections.erase(it);
        }
    }
    
    return aggregated_messages;
}

bool SocketPrimary::forward_to_server(const std::string& server_host, int server_port, 
                                       const std::string& register_name) {
    if (server_thread.joinable()) {
        return true;
    }
    
    server_thread = std::thread(&SocketPrimary::server_worker, this, 
                                server_host, server_port, register_name);
    return true;
}

void SocketPrimary::server_worker(const std::string& server_host, int server_port, 
                                  const std::string& register_name) {
    SocketServerConnector server_conn(register_name, server_host, server_port);
    
    while (listening) {
        if (!server_conn.is_connected()) {
            if (!server_conn.connect_to_server()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                continue;
            }
        }
        
        std::string messages = collect_messages();
        if (!messages.empty()) {
            if (!server_conn.send_message(messages)) {
                server_conn.disconnect();
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    server_conn.disconnect();
}

bool SocketPrimary::is_listening() const {
    return listening;
}

int SocketPrimary::get_client_count() const {
    std::lock_guard<std::mutex> lock(clients_mutex);
    return client_connections.size();
}

// socket_server_connector 实现
SocketServerConnector::SocketServerConnector(const std::string& register_name, 
                                                 const std::string& server_host, int port)
    : register_name(register_name), server_host(server_host), server_port(port),
      sockfd(-1), connected(false), running(false) {
}

SocketServerConnector::~SocketServerConnector() {
    stop_auto_reconnect();
    disconnect();
}

bool SocketServerConnector::connect_to_server_internal(int timeout_ms) {
    if (connected) {
        return true;
    }
    
    if (!utils::connect_to_tcp_server(server_host, server_port, sockfd, timeout_ms)) {
        return false;
    }
    
    // 发送register_name作为第一条消息
    if (!protocol::send_encoded_message(sockfd, register_name)) {
        utils::close_socket(sockfd);
        sockfd = -1;
        return false;
    }
    
    connected = true;
    return true;
}

bool SocketServerConnector::connect_to_server(int timeout_ms) {
    return connect_to_server_internal(timeout_ms);
}

bool SocketServerConnector::send_message(const std::string& message) {
    if (!connected) {
        return false;
    }
    
    return protocol::send_encoded_message(sockfd, message);
}

bool SocketServerConnector::disconnect() {
    if (sockfd != -1) {
        utils::close_socket(sockfd);
        sockfd = -1;
    }
    connected = false;
    return true;
}

bool SocketServerConnector::is_connected() const {
    return connected;
}

bool SocketServerConnector::reconnect(int max_attempts, int base_delay_ms) {
    disconnect();
    
    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        if (connect_to_server_internal(5000)) {
            return true;
        }
        
        if (attempt < max_attempts) {
            int delay_ms = base_delay_ms * (1 << (attempt - 1));
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
    }
    
    return false;
}

void SocketServerConnector::reconnect_worker(int max_attempts, int base_delay_ms) {
    while (running) {
        if (!connected) {
            reconnect(max_attempts, base_delay_ms);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

void SocketServerConnector::start_auto_reconnect(int max_attempts, int base_delay_ms) {
    if (running) {
        return;
    }
    
    running = true;
    reconnect_thread = std::thread(&SocketServerConnector::reconnect_worker, this, max_attempts, base_delay_ms);
}

void SocketServerConnector::stop_auto_reconnect() {
    if (!running) {
        return;
    }
    
    running = false;
    if (reconnect_thread.joinable()) {
        reconnect_thread.join();
    }
}

// protocol 命名空间实现
namespace protocol {

std::string encode_message(const std::string& content) {
    return std::to_string(content.length()) + "\n" + content;
}

std::string decode_message(const std::string& encoded) {
    size_t newline_pos = encoded.find('\n');
    if (newline_pos == std::string::npos) {
        return "";
    }
    
    std::string length_str = encoded.substr(0, newline_pos);
    int length = std::stoi(length_str);
    
    if (newline_pos + 1 + length > encoded.length()) {
        return "";
    }
    
    return encoded.substr(newline_pos + 1, length);
}

bool send_encoded_message(int sockfd, const std::string& message) {
    std::string encoded = encode_message(message);
    
    ssize_t bytes_sent = send(sockfd, encoded.c_str(), encoded.length(), 0);
    return bytes_sent == static_cast<ssize_t>(encoded.length());
}

std::string receive_encoded_message(int sockfd, int timeout_ms) {
    // 首先读取长度部分
    std::string length_str;
    char buffer;
    
    auto start_time = std::chrono::steady_clock::now();
    
    while (true) {
        struct pollfd pfd;
        pfd.fd = sockfd;
        pfd.events = POLLIN;
        
        int remaining_ms = timeout_ms;
        if (timeout_ms > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            remaining_ms = timeout_ms - elapsed;
            if (remaining_ms <= 0) {
                return "";
            }
        }
        
        int poll_result = poll(&pfd, 1, remaining_ms);
        if (poll_result <= 0) {
            return "";
        }
        
        ssize_t bytes_received = recv(sockfd, &buffer, 1, 0);
        if (bytes_received <= 0) {
            return "";
        }
        
        if (buffer == '\n') {
            break;
        }
        
        length_str += buffer;
    }
    
    if (length_str.empty()) {
        return "";
    }
    
    int length = std::stoi(length_str);
    if (length <= 0) {
        return "";
    }
    
    // 读取消息内容
    std::string message;
    message.resize(length);
    
    size_t total_received = 0;
    while (total_received < static_cast<size_t>(length)) {
        int remaining_ms = timeout_ms;
        if (timeout_ms > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            remaining_ms = timeout_ms - elapsed;
            if (remaining_ms <= 0) {
                return "";
            }
        }
        
        struct pollfd pfd;
        pfd.fd = sockfd;
        pfd.events = POLLIN;
        
        int poll_result = poll(&pfd, 1, remaining_ms);
        if (poll_result <= 0) {
            return "";
        }
        
        ssize_t bytes_received = recv(sockfd, &message[total_received], length - total_received, 0);
        if (bytes_received <= 0) {
            return "";
        }
        
        total_received += bytes_received;
    }
    
    return message;
}

} // namespace protocol

// config_parser 命名空间实现
namespace ConfigParser {

SocketConfig parse_config(const std::string& config_path) {
    std::ifstream file(config_path);
    if (!file.is_open()) {
        return SocketConfig();
    }
    
    nlohmann::json json_config;
    try {
        file >> json_config;
    } catch (const std::exception& e) {
        return SocketConfig();
    }
    
    return parse_config_from_json(json_config);
}

SocketConfig parse_config_from_json(const nlohmann::json& json_config) {
    SocketConfig config;
    
    try {
        // 解析clients
        if (json_config.contains("clients")) {
            for (const auto& client : json_config["clients"]) {
                config.clients.push_back(client.get<std::string>());
            }
        }
        
        // 解析primary配置
        if (json_config.contains("primary")) {
            const auto& primary = json_config["primary"];
            config.primary.hostname = primary["hostname"].get<std::string>();
            config.primary.port = primary["port"].get<int>();
        }
        
        // 解析server配置
        if (json_config.contains("server")) {
            const auto& server = json_config["server"];
            config.server.host = server["host"].get<std::string>();
            config.server.port = server["port"].get<int>();
            config.server.register_name = server["register_name"].get<std::string>();
        }
    } catch (const std::exception& e) {
        // 返回默认配置
    }
    
    return config;
}

} // namespace config_parser

// utils 命名空间实现
namespace utils {

bool set_socket_timeout(int sockfd, int timeout_ms) {
    if (sockfd < 0) {
        return false;
    }
    
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        return false;
    }
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        return false;
    }
    
    return true;
}

bool is_socket_valid(int sockfd) {
    return fcntl(sockfd, F_GETFD) != -1 || errno != EBADF;
}

void close_socket(int sockfd) {
    if (sockfd >= 0) {
        close(sockfd);
    }
}

std::string get_hostname() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return std::string(hostname);
    }
    return "unknown";
}

bool create_tcp_server(int port, int& sockfd, int backlog) {
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        return false;
    }
    
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close_socket(sockfd);
        sockfd = -1;
        return false;
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close_socket(sockfd);
        sockfd = -1;
        return false;
    }
    
    if (listen(sockfd, backlog) < 0) {
        close_socket(sockfd);
        sockfd = -1;
        return false;
    }
    
    return true;
}

bool connect_to_tcp_server(const std::string& host, int port, int& sockfd, int timeout_ms) {
    struct hostent* host_entry = gethostbyname(host.c_str());
    if (!host_entry) {
        return false;
    }
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        return false;
    }
    
    // 设置非阻塞模式
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    memcpy(&server_addr.sin_addr, host_entry->h_addr, host_entry->h_length);
    
    int result = connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (result < 0 && errno != EINPROGRESS) {
        close_socket(sockfd);
        sockfd = -1;
        return false;
    }
    
    if (result == 0) {
        // 连接立即建立
        fcntl(sockfd, F_SETFL, flags);
        return true;
    }
    
    // 等待连接建立
    struct pollfd pfd;
    pfd.fd = sockfd;
    pfd.events = POLLOUT;
    
    int poll_result = poll(&pfd, 1, timeout_ms);
    if (poll_result <= 0) {
        close_socket(sockfd);
        sockfd = -1;
        return false;
    }
    
    // 检查连接是否成功
    int error = 0;
    socklen_t len = sizeof(error);
    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
        close_socket(sockfd);
        sockfd = -1;
        return false;
    }
    
    // 恢复阻塞模式
    fcntl(sockfd, F_SETFL, flags);
    
    // 设置超时
    set_socket_timeout(sockfd, timeout_ms);
    
    return true;
}

} // namespace utils

} // namespace socket_utils