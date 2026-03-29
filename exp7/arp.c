#include "arp.h"
#include "base.h"
#include "types.h"
#include "ether.h"
#include "arpcache.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// handle arp packet
// If the dest ip address of this arp packet is not equal to the ip address of the incoming iface, drop it.
// If it is an arp request packet, send arp reply to the destination, insert the ip->mac mapping into arpcache.
// If it is an arp reply packet, insert the ip->mac mapping into arpcache.
// Tips:
// You can use functions: htons, htonl, ntohs, ntohl to convert host byte order and network byte order (16 bits use ntohs/htons, 32 bits use ntohl/htonl).
// You can use function: packet_to_ether_arp() in arp.h to get the ethernet header in a packet.
void handle_arp_packet(iface_info_t *iface, char *packet, int len)
{
	//assert(0 && "TODO: function handle_arp_packet not implemented!");
    struct ether_arp *arp_hdr = packet_to_ether_arp(packet);

    // 将目标IP地址从网络字节序转换为主机字节序，iface->ip是主机字节序
    u32 target_ip = ntohl(arp_hdr->arp_tpa);

    // 检查目标IP是否与接口的IP地址匹配
    if (target_ip != iface->ip) {
        // 如果不匹配，丢弃数据包
        //log(DEBUG, "Dropping ARP packet: target IP does not match interface IP.");
        return;
    }

    // 将发送方IP地址从网络字节序转换为主机字节序
    u32 sender_ip = ntohl(arp_hdr->arp_spa);

    // 根据ARP操作码判断是ARP请求还是ARP回复
    u16 arp_op = ntohs(arp_hdr->arp_op);
    if (arp_op != ARPOP_REQUEST && arp_op != ARPOP_REPLY) {
        // 如果操作码不是ARP请求或ARP回复，丢弃数据包
        //log(DEBUG, "Dropping ARP packet: unknown operation code.");
        return;
    }

    if (arp_op == ARPOP_REQUEST) {
        // 如果是ARP请求，发送ARP回复，并插入IP->MAC映射到ARP缓存
        //log(DEBUG, "Received ARP request, sending ARP reply.");
        arp_send_reply(iface, arp_hdr);
        arpcache_insert(sender_ip, arp_hdr->arp_sha);
    } else if (arp_op == ARPOP_REPLY) {
        // 如果是ARP回复，插入IP->MAC映射到ARP缓存
        //log(DEBUG, "Received ARP reply, updating ARP cache.");
        arpcache_insert(sender_ip, arp_hdr->arp_sha);
    }
//检查完毕，也记得的用ntohl和ntohs进行序列转换了
}

// send an arp reply packet
// Encapsulate an arp reply packet, send it out through iface_send_packet.
void arp_send_reply(iface_info_t *iface, struct ether_arp *req_hdr)
{
	//assert(0 && "TODO: function arp_send_reply not implemented!");
	
    char *packet = (char *)malloc(ETHER_HDR_SIZE + sizeof(struct ether_arp));
    if (!packet) {
        //log(ERROR, "Failed to allocate memory for ARP reply packet.");
        return;
    }
//待对照3.2协议格式确认对错，按照arp包要求填充，包括request
//记得用htonl,htons等函数，以及memcpy,sizeof,
//还有ETHER_HDR_SIZE,ETH_ALEN,ARPOP_REPLY,ETH_P_ARP,
//ARPHRD_ETHER,ETH_P_IP,ARPOP_REQUES等宏，按照实验文档的包格式来就行。
//记得参考record.txt
    // 获取以太网头部
    struct ether_header *eth_hdr = (struct ether_header *)packet;
    struct ether_arp *arp_hdr = (struct ether_arp *)(packet + ETHER_HDR_SIZE);

    // 填充以太网头部
    memcpy(eth_hdr->ether_dhost, req_hdr->arp_sha, ETH_ALEN); // 目标MAC地址为请求者的MAC地址
    memcpy(eth_hdr->ether_shost, iface->mac, ETH_ALEN);       // 源MAC地址为接口的MAC地址
    eth_hdr->ether_type = htons(ETH_P_ARP);                  // 以太网类型为ARP

    // 填充ARP头部
    arp_hdr->arp_hrd = htons(ARPHRD_ETHER);                  // 硬件类型为以太网
    arp_hdr->arp_pro = htons(ETH_P_IP);                      // 协议类型为IP
    arp_hdr->arp_hln = ETH_ALEN;                             // 硬件地址长度
    arp_hdr->arp_pln = 4;                                    // 协议地址长度
    arp_hdr->arp_op = htons(ARPOP_REPLY);                    // 操作码为ARP Reply

    // 填充ARP地址字段，req_hdr本身就是网络字节序
    memcpy(arp_hdr->arp_sha, iface->mac, ETH_ALEN);          // 源MAC地址为接口的MAC地址
    arp_hdr->arp_spa = req_hdr->arp_tpa;                     // 源IP地址为请求中的目标IP地址
    memcpy(arp_hdr->arp_tha, req_hdr->arp_sha, ETH_ALEN);    // 目标MAC地址为请求者的MAC地址
    arp_hdr->arp_tpa = req_hdr->arp_spa;                     // 目标IP地址为请求者的IP地址

   // 通过接口发送封装好的ARP Reply数据包
    iface_send_packet(iface, packet, ETHER_HDR_SIZE + sizeof(struct ether_arp));

    // 释放分配的内存
  //  free(packet);

//简单检查完毕，应该没问题
}

// send an arp request
// Encapsulate an arp request packet, send it out through iface_send_packet.
void arp_send_request(iface_info_t *iface, u32 dst_ip)
{
	//assert(0 && "TODO: function arp_send_request not implemented!");
	    // 分配内存用于封装ARP Request数据包
    log(DEBUG, "arp_send_request");
    
        char *packet = (char *)malloc(ETHER_HDR_SIZE + sizeof(struct ether_arp));
    if (!packet) {
        log(ERROR, "Failed to allocate memory for ARP request packet.");
        return;
    }

    // 获取以太网头部
    struct ether_header *eth_hdr = (struct ether_header *)packet;
    struct ether_arp *arp_hdr = (struct ether_arp *)(packet + ETHER_HDR_SIZE);

    // 填充以太网头部
    memset(eth_hdr->ether_dhost, 0xff, ETH_ALEN);            // 目标MAC地址为广播地址
    memcpy(eth_hdr->ether_shost, iface->mac, ETH_ALEN);       // 源MAC地址为接口的MAC地址
    eth_hdr->ether_type = htons(ETH_P_ARP);                  // 以太网类型为ARP

    // 填充ARP头部
    arp_hdr->arp_hrd = htons(ARPHRD_ETHER);                  // 硬件类型为以太网
    arp_hdr->arp_pro = htons(ETH_P_IP);                      // 协议类型为IP
    arp_hdr->arp_hln = ETH_ALEN;                             // 硬件地址长度
    arp_hdr->arp_pln = 4;                                    // 协议地址长度
    arp_hdr->arp_op = htons(ARPOP_REQUEST);                  // 操作码为ARP Request

    // 填充ARP地址字段
    memcpy(arp_hdr->arp_sha, iface->mac, ETH_ALEN);          // 源MAC地址为接口的MAC地址
    arp_hdr->arp_spa = htonl(iface->ip);                     // 源IP地址为接口的IP地址
    memset(arp_hdr->arp_tha, 0x00, ETH_ALEN);                // 目标MAC地址为空
    arp_hdr->arp_tpa = htonl(dst_ip);                        // 目标IP地址为目标IP

    // 通过接口发送封装好的ARP Request数据包
    iface_send_packet(iface, packet, ETHER_HDR_SIZE + sizeof(struct ether_arp));

    // 释放分配的内存
    //free(packet);

//简单检查完毕，应该没问题
}

// send (IP) packet through arpcache lookup 
// Lookup the mac address of dst_ip in arpcache.
// If it is found, fill the ethernet header and emit the packet by iface_send_packet.
// Otherwise, pending this packet into arpcache and send arp request.
void iface_send_packet_by_arp(iface_info_t *iface, u32 dst_ip, char *packet, int len)
{
    log(DEBUG,"iface_send_packet_by_arp");
	//assert(0 && "TODO: function iface_send_packet_by_arp not implemented!");
	    // 在ARP缓存中查找目标IP的MAC地址
    u8 mac[ETH_ALEN];
    int found = arpcache_lookup(dst_ip, mac);

    if (found) {
        log(DEBUG, "Found MAC address in ARP cache, sending packet.");
        // 如果找到目标MAC地址，填充以太网帧头部
        struct ether_header *eth_hdr = (struct ether_header *)packet;
        memcpy(eth_hdr->ether_dhost, mac, ETH_ALEN);         // 目标MAC地址
        memcpy(eth_hdr->ether_shost, iface->mac, ETH_ALEN);  // 源MAC地址
        eth_hdr->ether_type = htons(ETH_P_IP);               // 以太网类型为IP


        // 通过接口发送数据包
        iface_send_packet(iface, packet, len);

    } else {
        log(DEBUG, "MAC address not found in ARP cache, pending packet.");
        // 如果未找到目标MAC地址，将数据包挂入ARP缓存的等待队列
        arpcache_append_packet(iface, dst_ip, packet, len);

        // 发送ARP Request以获取目标MAC地址
        arp_send_request(iface, dst_ip);
    }
//检查完毕，应该没问题
}
