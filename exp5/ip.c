#include "ip.h"
#include "icmp.h"
#include "arpcache.h"
#include "rtable.h"
#include "arp.h"
#include "log.h"
#include <stdlib.h>
#include <assert.h>

// If the packet is ICMP echo request and the destination IP address is equal to the IP address of the iface, send ICMP echo reply.
// Otherwise, forward the packet.
// Tips:
// You can use struct iphdr *ip = packet_to_ip_hdr(packet); in ip.h to get the ip header in a packet.
// You can use struct icmphdr *icmp = (struct icmphdr *)IP_DATA(ip); in ip.h to get the icmp header in a packet.
void handle_ip_packet(iface_info_t *iface, char *packet, int len)
{
	//assert(0 && "TODO: function handle_ip_packet not implemented!");
	struct iphdr *ip = packet_to_ip_hdr(packet);
         // 打印目标IP地址

         
    struct in_addr daddr;
    daddr.s_addr = ip->daddr; // 保持网络字节序
    printf("handle_ip_packet: daddr = %s\n", inet_ntoa(daddr));



    struct icmphdr *icmp = (struct icmphdr *)IP_DATA(ip);
    // 检查目标IP是否等于接口的IP地址
    if (ntohl(ip->daddr) == iface->ip && icmp->type == ICMP_ECHOREQUEST) {
        // 如果是ICMP Echo Request，发送ICMP Echo Reply
        log(DEBUG, "Received ICMP Echo Request, sending Echo Reply.");
        
        
   // struct in_addr daddr;
   // daddr.s_addr = ip->daddr; // 网络字节序
  //  printf("ip_send_packet in handle_ip_packet: daddr = %s\n", inet_ntoa(daddr));
        
        icmp_send_packet(packet, len, ICMP_ECHOREPLY, 0);
        free(packet); // 释放数据包内存
    } else {
        // 否则转发数据包
        ip_forward_packet(ntohl(ip->daddr), packet, len);
        //这里不需要free(packet)，因为在ip_forward_packet后续函数调用会处理
    }
//基本检查完毕，这个函数添加了icmp发送后free packet的操作
}

// When forwarding the packet, you should check the TTL, update the checksum and TTL.
// Then, determine the next hop to forward the packet, then send the packet by iface_send_packet_by_arp.
// The interface to forward the packet is specified by longest_prefix_match.
void ip_forward_packet(u32 ip_dst, char *packet, int len)
{
    log(DEBUG,"ip_forward_packet");

	//assert(0 && "TODO: function ip_forward_packet not implemented!");
	struct iphdr *ip = packet_to_ip_hdr(packet);

     // 打印目标IP地址
    struct in_addr daddr;
    daddr.s_addr = ip->daddr; // 保持网络字节序
    printf("ip_forward_packet: daddr = %s\n", inet_ntoa(daddr));

//先判断TTL，到点了发ICMP包，free packet（traceroute的关键），
//否则和ip_send_packet类似找路径，下一跳，调用函数
    // 检查TTL是否有效
    if (ip->ttl <= 1) {
        log(DEBUG, "TTL expired, sending ICMP Time Exceeded.");
       
        icmp_send_packet(packet, len, ICMP_TIME_EXCEEDED, ICMP_EXC_TTL);
        free(packet); // 释放数据包内存
        return;
    }

    // 更新TTL和校验和
    ip->ttl -= 1;
    ip->checksum = ip_checksum(ip);

    // 查找路由表，确定下一跳
    rt_entry_t *entry = longest_prefix_match(ip_dst);
    if (!entry) {
        // 如果无法找到路由表条目，发送ICMP Destination Unreachable
        log(DEBUG, "No route to host, sending ICMP Destination Unreachable.");
        icmp_send_packet(packet, len, ICMP_DEST_UNREACH, ICMP_NET_UNREACH);
        free(packet); // 释放数据包内存
        return;
    }

    // 确定下一跳地址
    u32 next_hop = (entry->gw == 0) ? ip_dst : entry->gw;

    // 通过接口发送数据包
    iface_send_packet_by_arp(entry->iface, next_hop, packet, len);//这个函数会自己free packet
//基本检查完毕，符合要求
}