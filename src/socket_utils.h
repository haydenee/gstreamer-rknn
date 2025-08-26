#ifndef __SOCKET_UTILS_H__
#define __SOCKET_UTILS_H__

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include "nlohmann/json.hpp"

namespace SocketUtils {

// 配置结构体
struct SocketConfig {
    std::vector<std::string> clients;
    struct primary_config {
        std::string hostname;
        int port;
    } primary;
    struct server_config {
        std::string host;
        int port;
        std::string register_name;
    } server;
};

// Socket客户端类
class SocketClient {
private:
    std::string hostname;
    std::string primary_host;
    int primary_port;
    int sockfd;
    std::atomic<bool> connected;
    std::atomic<bool> running;
    std::thread reconnect_thread;
    
    bool connect_to_primary_internal(int timeout_ms);
    void reconnect_worker(int max_attempts, int base_delay_ms);
    
public:
    SocketClient(const std::string& hostname, const std::string& primary_host, int port);
    ~SocketClient();
    
    bool connect_to_primary(int timeout_ms = 5000);
    bool send_message(const std::string& message);
    bool disconnect();
    bool is_connected() const;
    bool reconnect(int max_attempts = 3, int base_delay_ms = 1000);
    void start_auto_reconnect(int max_attempts = 3, int base_delay_ms = 1000);
    void stop_auto_reconnect();
};

// Socket主节点类
class SocketPrimary {
private:
    std::string hostname;
    int listen_port;
    int server_sockfd;
    std::unordered_map<int, std::string> client_connections;
    mutable std::mutex clients_mutex;
    std::atomic<bool> listening;
    std::thread accept_thread;
    std::thread server_thread;
    
    void accept_connections_worker();
    void server_worker(const std::string& server_host, int server_port, 
                      const std::string& register_name);
    
public:
    SocketPrimary(const std::string& hostname, int port);
    ~SocketPrimary();
    
    bool start_listening(int backlog = 10);
    void stop_listening();
    std::string collect_messages();
    bool forward_to_server(const std::string& server_host, int server_port, 
                          const std::string& register_name);
    bool is_listening() const;
    int get_client_count() const;
};

// Socket服务器连接类
class SocketServerConnector {
private:
    std::string register_name;
    std::string server_host;
    int server_port;
    int sockfd;
    std::atomic<bool> connected;
    std::atomic<bool> running;
    std::thread reconnect_thread;
    
    bool connect_to_server_internal(int timeout_ms);
    void reconnect_worker(int max_attempts, int base_delay_ms);
    
public:
    SocketServerConnector(const std::string& register_name, 
                           const std::string& server_host, int port);
    ~SocketServerConnector();
    
    bool connect_to_server(int timeout_ms = 5000);
    bool send_message(const std::string& message);
    bool disconnect();
    bool is_connected() const;
    bool reconnect(int max_attempts = 3, int base_delay_ms = 1000);
    void start_auto_reconnect(int max_attempts = 3, int base_delay_ms = 1000);
    void stop_auto_reconnect();
};

// 消息协议处理函数
namespace protocol {
    std::string encode_message(const std::string& content);
    std::string decode_message(const std::string& encoded);
    bool send_encoded_message(int sockfd, const std::string& message);
    std::string receive_encoded_message(int sockfd, int timeout_ms = 5000);
}

// 配置解析函数
namespace ConfigParser {
    SocketConfig parse_config(const std::string& config_path);
    SocketConfig parse_config_from_json(const nlohmann::json& json_config);
}

// Socket工具函数
namespace utils {
    bool set_socket_timeout(int sockfd, int timeout_ms);
    bool is_socket_valid(int sockfd);
    void close_socket(int sockfd);
    std::string get_hostname();
    bool create_tcp_server(int port, int& sockfd, int backlog = 10);
    bool connect_to_tcp_server(const std::string& host, int port, int& sockfd, int timeout_ms = 5000);
}

} // namespace socket_utils

#endif // __SOCKET_UTILS_H__
