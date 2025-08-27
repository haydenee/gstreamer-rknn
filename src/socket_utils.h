#ifndef __SOCKET_UTILS_H__
#define __SOCKET_UTILS_H__

#include <string>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include "rknn_meta.h"

// C-compatible forward declarations for use in C headers
#ifdef __cplusplus
class SocketUtils;
class SocketClient;
class SocketPrimary;
#else
typedef struct SocketUtils SocketUtils;
typedef struct SocketClient SocketClient;
typedef struct SocketPrimary SocketPrimary;
#endif

// 工具类，提供静态方法用于消息格式处理
class SocketUtils {
public:
    /**
     * 发送格式化的消息（长度+\n+内容）
     * @param sockfd socket文件描述符
     * @param message 要发送的消息内容
     * @return 成功返回true，失败返回false
     */
    static bool send_message(int sockfd, const std::string& message);

    /**
     * 接收格式化的消息（长度+\n+内容）
     * @param sockfd socket文件描述符
     * @param message 接收到的消息内容
     * @return 成功返回true，失败返回false
     */
    static bool receive_message(int sockfd, std::string& message);

    /**
     * 连接到指定服务器
     * @param host 服务器主机名或IP地址
     * @param port 服务器端口
     * @return 成功返回socket文件描述符，失败返回-1
     */
    static int connect_to_server(const std::string& host, int port);

    /**
     * 创建服务器socket并监听
     * @param port 监听端口
     * @return 成功返回socket文件描述符，失败返回-1
     */
    static int create_server(int port);
};

// SocketClient类，用于连接primary并发送检测信息
class SocketClient {
private:
    int sockfd_;
    std::string hostname_;
    std::string primary_host_;
    int primary_port_;
    bool connected_;

public:
    /**
     * 构造函数
     * @param hostname 客户端的hostname
     */
    SocketClient(const std::string& hostname);

    /**
     * 析构函数，自动断开连接
     */
    ~SocketClient();

    /**
     * 连接到primary服务器
     * @param primary_host primary服务器主机名或IP地址
     * @param primary_port primary服务器端口
     * @return 成功返回true，失败返回false
     */
    bool connect(const std::string& primary_host, int primary_port);

    /**
     * 发送检测信息到primary
     * @param detection_info 检测信息内容
     * @return 成功返回true，失败返回false
     */
    bool send_detection(RknnMeta* rknn_meta);

    /**
     * 断开与primary的连接
     */
    void disconnect();

    /**
     * 检查是否已连接
     * @return 已连接返回true，否则返回false
     */
    bool is_connected() const { return connected_; }

    /**
     * 获取hostname
     * @return hostname
     */
    std::string get_hostname() const { return hostname_; }
};

// SocketPrimary类，用于接收clients信息并转发给server
class SocketPrimary {
private:
    int server_sockfd_;      // 连接到server的socket
    int listen_sockfd_;      // 监听clients连接的socket
    std::string register_name_;
    std::string server_host_;
    int server_port_;
    int client_port_;
    bool connected_to_server_;

public:
    /**
     * 构造函数
     * @param register_name 注册到server的名称
     */
    SocketPrimary(const std::string& register_name);

    /**
     * 析构函数，自动断开所有连接
     */
    ~SocketPrimary();

    /**
     * 连接到后端server
     * @param server_host server主机名或IP地址
     * @param server_port server端口
     * @return 成功返回true，失败返回false
     */
    bool connect_to_server(const std::string& server_host, int server_port);

    /**
     * 开始监听client连接
     * @param client_port 监听client连接的端口
     * @return 成功返回true，失败返回false
     */
    bool start_listening(int client_port);

    /**
     * 接受一个client连接
     * @param client_sockfd 输出参数，接收client的socket文件描述符
     * @param client_hostname 输出参数，接收client的hostname
     * @return 成功返回true，失败返回false
     */
    bool accept_client(int& client_sockfd, std::string& client_hostname);

    /**
     * 发送汇总信息到server
     * @param aggregated_info 汇总的信息内容
     * @return 成功返回true，失败返回false
     */
    bool send_to_server(const std::string& aggregated_info);

    /**
     * 断开与server的连接
     */
    void disconnect_from_server();

    /**
     * 检查是否已连接到server
     * @return 已连接返回true，否则返回false
     */
    bool is_connected_to_server() const { return connected_to_server_; }

    /**
     * 获取register name
     * @return register name
     */
    std::string get_register_name() const { return register_name_; }
};

#endif /* __SOCKET_UTILS_H__ */