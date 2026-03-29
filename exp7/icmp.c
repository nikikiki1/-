#include "icmp.h"
#include "ip.h"
#include "rtable.h"
#include "arp.h"
#include "base.h"
#include "log.h"
#include <stdlib.h>
#include <assert.h>

// icmp_send_packet has two main functions:
// 1.handle icmp packets sent to the router itself (ICMP ECHO REPLY).
// 2.when an error occurs, send icmp error packets.
// Note that the structure of these two icmp packets is different, you need to malloc different sizes of memory.
// Some function and macro definitions in ip.h/icmp.h can help you.
void icmp_send_packet(const char *in_pkt, int len, u8 type, u8 code)
{
   log(DEBUG, "Sending ICMP packet, type: %d, code: %d\n", type, code);
    struct iphdr *in_ip = packet_to_ip_hdr(in_pkt);

    int icmp_len;
    char *packet;

    if (type == ICMP_ECHOREPLY) {
        // 回显应答，长度为原始ICMP包长度
        icmp_len = len - ETHER_HDR_SIZE - IP_BASE_HDR_SIZE;
        int total_len = ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + icmp_len;
        packet = (char *)malloc(total_len);

        // 以太网头部复制
        memcpy(packet, in_pkt, ETHER_HDR_SIZE);


        // IP头部
        struct iphdr *ip = (struct iphdr *)(packet + ETHER_HDR_SIZE);
        ip_init_hdr(ip, ntohl(in_ip->daddr), ntohl(in_ip->saddr), IP_BASE_HDR_SIZE + icmp_len, IPPROTO_ICMP);

        // ICMP内容复制
        memcpy(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE,
               in_pkt + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE,
               icmp_len);

        // 设置ICMP类型和校验和
        struct icmphdr *icmp = (struct icmphdr *)(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE);
        icmp->type = ICMP_ECHOREPLY;
        icmp->code = 0;
        icmp->checksum = 0;
        icmp->checksum = icmp_checksum(icmp, icmp_len);

        ip_send_packet(packet, total_len);
    } else {
        // ICMP错误包，数据部分为原IP头+8字节数据
        icmp_len = ICMP_HDR_SIZE + IP_BASE_HDR_SIZE + 8;
        int total_len = ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + icmp_len;
        packet = (char *)malloc(total_len);

        // 以太网头部复制
        memcpy(packet, in_pkt, ETHER_HDR_SIZE);


            // 查找路由表，确定下一跳
         rt_entry_t *entry = longest_prefix_match(ntohl(in_ip->saddr));


    
        if (!entry) {
        // 如果无法找到路由表条目，记录错误并丢弃数据包
            log(ERROR, "No route to host for IP packet.");
            free(packet);
            return;
        }

        // IP头部
        struct iphdr *ip = (struct iphdr *)(packet + ETHER_HDR_SIZE);
        ip_init_hdr(ip, entry->iface->ip, ntohl(in_ip->saddr), IP_BASE_HDR_SIZE + icmp_len, IPPROTO_ICMP);

        // ICMP头部
        struct icmphdr *icmp = (struct icmphdr *)(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE);
        icmp->type = type;
        icmp->code = code;
        icmp->checksum = 0;
        icmp->icmp_identifier = 0;
        icmp->icmp_sequence = 0;

        // 拷贝原IP头和8字节数据
        memcpy((char *)icmp + ICMP_HDR_SIZE,
               in_pkt + ETHER_HDR_SIZE,
               IP_BASE_HDR_SIZE + 8);

        // 校验和
        icmp->checksum = icmp_checksum(icmp, icmp_len);

        ip_send_packet(packet, total_len);
    }
}
