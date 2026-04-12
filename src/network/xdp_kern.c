#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/in.h>
#include <linux/types.h>
#include <bpf/bpf_helpers.h>

/* Fallback for IPPROTO_UDP if header is missing */
#ifndef IPPROTO_UDP
#define IPPROTO_UDP 17
#endif

typedef __u16 u16;
typedef __u32 u32;

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 256);
    __type(key, u16);
    __type(value, u32);
} buan_config_map SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_XSKMAP);
    __uint(max_entries, 64);
    __type(key, int);
    __type(value, int);
} buan_xsk_map SEC(".maps");

SEC("xdp")
int buan_gatekeeper_prog(struct xdp_md* ctx) {
    void* data_end = (void*)(unsigned long)ctx->data_end;
    void* data     = (void*)(unsigned long)ctx->data;

    struct ethhdr* eth = data;
    if ((void*)(eth + 1) > data_end) return XDP_PASS;

    if (eth->h_proto != __constant_htons(ETH_P_IP)) return XDP_PASS;

    struct iphdr* iph = (void*)((unsigned char*)data + sizeof(*eth));
    if ((void*)(iph + 1) > data_end) return XDP_PASS;

    if (iph->protocol != IPPROTO_UDP) return XDP_PASS;

    struct udphdr* udp = (void*)((unsigned char*)iph + ((unsigned long)iph->ihl * 4));
    if ((void*)(udp + 1) > data_end) return XDP_PASS;

    u16 dport = __constant_ntohs(udp->dest);

    u32* action = bpf_map_lookup_elem(&buan_config_map, &dport);
    if (action) {
        return bpf_redirect_map(&buan_xsk_map, ctx->rx_queue_index, XDP_PASS);
    }

    return XDP_PASS;
}

char license[] SEC("_license") = "GPL";