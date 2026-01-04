#!/usr/bin/env python3
"""
检查并安装 mDNS 支持工具 (avahi-utils)
读取 socket_config.json 配置文件，检查所有提到的 .local 主机名
并确保远程设备安装了必要的 mDNS 支持
"""

import json
import subprocess
import sys
import os
import re

def read_config(config_path):
    """读取 socket 配置文件"""
    try:
        with open(config_path, 'r') as f:
            return json.load(f)
    except FileNotFoundError:
        print(f"错误: 配置文件 {config_path} 不存在")
        return None
    except json.JSONDecodeError as e:
        print(f"错误: 配置文件格式错误: {e}")
        return None

def extract_hosts_from_config(config):
    """从配置中提取所有主机名"""
    hosts = set()
    
    # 检查 primary 配置
    if "primary" in config:
        primary = config["primary"]
        if "host" in primary:
            hosts.add(primary["host"])
        if "hostname" in primary:
            hosts.add(primary["hostname"])
    
    # 检查 server 配置
    if "server" in config:
        server = config["server"]
        if "host" in server:
            hosts.add(server["host"])
    
    # 检查 clients 列表
    if "clients" in config:
        for client in config["clients"]:
            hosts.add(client)
    
    return hosts

def check_mdns_hosts(hosts):
    """检查是否有 .local 主机名"""
    mdns_hosts = []
    for host in hosts:
        if host.endswith('.local'):
            mdns_hosts.append(host)
    
    return mdns_hosts

def check_avahi_installed(hostname):
    """检查远程设备是否安装了 avahi-utils"""
    hosts_to_try = [hostname, f"{hostname}.local"]
    
    for try_host in hosts_to_try:
        try:
            # 使用 ssh 连接到远程设备检查 avahi-utils
            cmd = f"ssh root@{try_host} 'dpkg -l | grep avahi-utils'"
            result = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=10)
            
            if result.returncode == 0 and 'avahi-utils' in result.stdout:
                print(f"✓ {try_host}: avahi-utils 已安装")
                return True
            else:
                print(f"✗ {try_host}: avahi-utils 未安装")
                # 继续尝试下一个主机名
                continue
                
        except subprocess.TimeoutExpired:
            print(f"✗ {try_host}: 连接超时")
            continue
        except Exception as e:
            print(f"✗ {try_host}: 检查失败: {e}")
            continue
    
    # 所有尝试都失败
    return False

def install_avahi_utils(hostname):
    """在远程设备上安装 avahi-utils"""
    hosts_to_try = [hostname, f"{hostname}.local"]
    
    for try_host in hosts_to_try:
        try:
            print(f"正在为 {try_host} 安装 avahi-utils...")
            
            # 更新包列表并安装 avahi-utils
            cmd = f"ssh root@{try_host} 'apt update && apt install -y avahi-utils'"
            result = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=120)
            
            if result.returncode == 0:
                print(f"✓ {try_host}: avahi-utils 安装成功")
                return True
            else:
                print(f"✗ {try_host}: avahi-utils 安装失败")
                print(f"错误输出: {result.stderr}")
                # 继续尝试下一个主机名
                continue
                
        except subprocess.TimeoutExpired:
            print(f"✗ {try_host}: 安装超时")
            continue
        except Exception as e:
            print(f"✗ {try_host}: 安装失败: {e}")
            continue
    
    # 所有尝试都失败
    return False

def main():
    # 读取配置文件
    config_path = "socket_config.json"
    config = read_config(config_path)
    if not config:
        sys.exit(1)
    
    # 检查是否启用 socket 功能
    if not config.get("enable", False):
        print("Socket 功能未启用，跳过 mDNS 检查")
        return
    
    print("=== mDNS 支持检查 ===")
    
    # 提取所有主机名
    hosts = extract_hosts_from_config(config)
    print(f"配置中的主机名: {', '.join(hosts)}")
    
    # 检查是否有 .local 主机名
    mdns_hosts = check_mdns_hosts(hosts)
    if not mdns_hosts:
        print("未发现 .local 主机名，无需 mDNS 支持")
        return
    
    print(f"发现 mDNS 主机名: {', '.join(mdns_hosts)}")
    
    # 获取所有需要检查的远程主机（去掉 .local 后缀）
    remote_hosts = set()
    for host in mdns_hosts:
        if host.endswith('.local'):
            base_host = host[:-6]  # 去掉 .local 后缀
            remote_hosts.add(base_host)
    
    print(f"需要检查的远程主机: {', '.join(remote_hosts)}")
    
    # 检查每个远程主机的 avahi-utils 安装状态
    needs_installation = []
    for host in remote_hosts:
        if not check_avahi_installed(host):
            needs_installation.append(host)
    
    if not needs_installation:
        print("✓ 所有远程设备都已安装 avahi-utils")
        return
    
    print(f"需要安装 avahi-utils 的设备: {', '.join(needs_installation)}")
    
    # 自动安装 avahi-utils
    print("自动安装 avahi-utils...")
    success_count = 0
    for host in needs_installation:
        if install_avahi_utils(host):
            success_count += 1
    
    print(f"安装完成: {success_count}/{len(needs_installation)} 台设备成功")

if __name__ == "__main__":
    main()