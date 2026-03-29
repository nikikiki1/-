#include "arpcache.h"
#include "arp.h"
#include "ether.h"
#include "icmp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <assert.h>
#include "log.h"

static arpcache_t arpcache;


// initialize IP->mac mapping, request list, lock and sweep thread
void arpcache_init()
{
	bzero(&arpcache, sizeof(arpcache_t));

	init_list_head(&(arpcache.req_list));

	pthread_mutex_init(&arpcache.lock, NULL);

	pthread_create(&arpcache.thread, NULL, arpcache_sweep, NULL);
}

// release all the resources when exiting
void arpcache_destroy()
{
	pthread_mutex_lock(&arpcache.lock);

	struct arp_req *req_entry = NULL, *req_q;
	list_for_each_entry_safe(req_entry, req_q, &(arpcache.req_list), list) {
		struct cached_pkt *pkt_entry = NULL, *pkt_q;
		list_for_each_entry_safe(pkt_entry, pkt_q, &(req_entry->cached_packets), list) {
			list_delete_entry(&(pkt_entry->list));
			free(pkt_entry->packet);
			free(pkt_entry);
		}

		list_delete_entry(&(req_entry->list));
		free(req_entry);
	}

	pthread_kill(arpcache.thread, SIGTERM);

	pthread_mutex_unlock(&arpcache.lock);
}

// look up the IP->mac mapping, need pthread_mutex_lock/unlock
// Traverse the table to find whether there is an entry with the same IP and mac address with the given arguments.
int arpcache_lookup(u32 ip4, u8 mac[ETH_ALEN])
{


    log(DEBUG, "arpcache_lookup");
	//assert(0 && "TODO: function arpcache_lookup not implemented!");
	//return 0;
	int found = 0;

    // 加锁以保护ARP缓存表
    pthread_mutex_lock(&arpcache.lock);

    // 遍历ARP缓存表，查找目标IP地址对应的MAC地址
    for (int i = 0; i < MAX_ARP_SIZE; i++) {
        if (arpcache.entries[i].valid && arpcache.entries[i].ip4 == ip4) {
            // 如果找到匹配的条目，复制MAC地址到输出参数
            memcpy(mac, arpcache.entries[i].mac, ETH_ALEN);
            found = 1;
            break;
        }
    }

    // 解锁
    pthread_mutex_unlock(&arpcache.lock);

    return found;
    //检查完毕，基本正确
}

// insert the IP->mac mapping into arpcache, need pthread_mutex_lock/unlock
// If there is a timeout entry (attribute valid in struct) in arpcache, replace it.
// If there isn't a timeout entry in arpcache, randomly replace one.
// If there are pending packets waiting for this mapping, fill the ethernet header for each of them, and send them out.
// Tips:
// arpcache_t是完整的arp缓存表，里边的req_list是一个链表，它的每个节点(用arp_req结构体封装)里又存着一个链表头，这些二级链表(节点类型是cached_pkt)缓存着相同目标ip但不知道mac地址的包
void arpcache_insert(u32 ip4, u8 mac[ETH_ALEN])
{
	//assert(0 && "TODO: function arpcache_insert not implemented!");
	// 加锁以保护ARP缓存表
    pthread_mutex_lock(&arpcache.lock);

    int replaced = 0;

    // 遍历ARP缓存表，查找超时条目
    for (int i = 0; i < MAX_ARP_SIZE; i++) {
        if (!arpcache.entries[i].valid) {
            // 替换超时条目
            arpcache.entries[i].ip4 = ip4;
            memcpy(arpcache.entries[i].mac, mac, ETH_ALEN);
            arpcache.entries[i].valid = 1;
            arpcache.entries[i].added = time(NULL); // 记录插入时间
            replaced = 1;
            break;
        }
    }

    // 如果没有找到超时条目，随机替换一个条目
    if (!replaced) {
        int idx = rand() % MAX_ARP_SIZE;
        arpcache.entries[idx].ip4 = ip4;
        memcpy(arpcache.entries[idx].mac, mac, ETH_ALEN);
        arpcache.entries[idx].valid = 1;
        arpcache.entries[idx].added = time(NULL); // 记录插入时间
    }

    // 遍历等待队列，处理等待此IP->MAC映射的待发送数据包
    struct arp_req *req_entry = NULL, *req_q;
    list_for_each_entry_safe(req_entry, req_q, &(arpcache.req_list), list) {
        if (req_entry->ip4 == ip4) {
            // 遍历此请求的所有待发送数据包
            struct cached_pkt *pkt_entry = NULL, *pkt_q;
            list_for_each_entry_safe(pkt_entry, pkt_q, &(req_entry->cached_packets), list) {
                // 填充以太网头部
                struct ether_header *eth_hdr = (struct ether_header *)pkt_entry->packet;
                memcpy(eth_hdr->ether_dhost, mac, ETH_ALEN);         // 目标MAC地址
                // 无需填充源MAC，只要发送出去就行了，这些数据包是积压在请求列表中的目标IP一致的包
                memcpy(eth_hdr->ether_shost, req_entry->iface->mac, ETH_ALEN); // 源MAC地址
                eth_hdr->ether_type = htons(ETH_P_IP);               // 以太网类型为IP

                // 发送数据包
                iface_send_packet(req_entry->iface, pkt_entry->packet, pkt_entry->len);

                // 释放已发送的数据包
                list_delete_entry(&(pkt_entry->list));
               // free(pkt_entry->packet);
                free(pkt_entry);
            }

            // 删除请求条目
            list_delete_entry(&(req_entry->list));
            free(req_entry);
        }
    }
    // 解锁
    pthread_mutex_unlock(&arpcache.lock);
    //检查完毕，基本无影响
}

// append the packet to arpcache
// Look up in the list which stores pending packets, if there is already an entry with the same IP address and iface, 
// which means the corresponding arp request has been sent out, just append this packet at the tail of that entry (The entry may contain more than one packet).
// Otherwise, malloc a new entry with the given IP address and iface, append the packet, and send arp request.
// Tips:
// arpcache_t是完整的arp缓存表，里边的req_list是一个链表，它的每个节点(类型是arp_req)里又存着一个链表头，这些二级链表(节点类型是cached_pkt)缓存着相同目标ip但不知道mac地址的包
void arpcache_append_packet(iface_info_t *iface, u32 ip4, char *packet, int len)
{
	//assert(0 && "TODO: function arpcache_append_packet not implemented!");
	    // 加锁以保护ARP缓存表和请求列表
    
    log(DEBUG, "arpcache_append_packet");

        pthread_mutex_lock(&arpcache.lock);

    struct arp_req *req_entry = NULL;

    // 遍历请求列表，查找是否已存在相同IP地址的请求条目
    list_for_each_entry(req_entry, &(arpcache.req_list), list) {
        if (req_entry->ip4 == ip4 && req_entry->iface == iface) {
            // 如果找到相同IP地址和接口的请求条目，将数据包附加到该条目的链表末尾
            struct cached_pkt *new_pkt = (struct cached_pkt *)malloc(sizeof(struct cached_pkt));
            new_pkt->packet = packet;
            new_pkt->len = len;
            list_add_tail(&(new_pkt->list), &(req_entry->cached_packets));
            pthread_mutex_unlock(&arpcache.lock);
            return;
        }
    }

    // 如果未找到相同IP地址的请求条目，创建一个新的请求条目
    req_entry = (struct arp_req *)malloc(sizeof(struct arp_req));
    req_entry->ip4 = ip4;
    req_entry->iface = iface;
    req_entry->retries = 0;
    req_entry->sent = time(NULL); // 记录发送时间
    init_list_head(&(req_entry->cached_packets));

    // 将数据包附加到新请求条目的链表
    struct cached_pkt *new_pkt = (struct cached_pkt *)malloc(sizeof(struct cached_pkt));
    new_pkt->packet = packet;
    new_pkt->len = len;
    list_add_tail(&(new_pkt->list), &(req_entry->cached_packets));

    // 将新请求条目添加到请求列表
    list_add_tail(&(req_entry->list), &(arpcache.req_list));

    // 发送ARP Request
    arp_send_request(iface, ip4);

    // 解锁
    pthread_mutex_unlock(&arpcache.lock);
//检查完毕，看上去没问题
}

// sweep arpcache periodically
// for IP->mac entry, if the entry has been in the table for more than 15 seconds, remove it from the table
// for pending packets, if the arp request is sent out 1 second ago, while the reply has not been received, retransmit the arp request
// If the arp request has been sent 5 times without receiving arp reply, for each pending packet, send icmp packet (DEST_HOST_UNREACHABLE), and drop these packets
// tips
// arpcache_t是完整的arp缓存表，里边的req_list是一个链表，它的每个节点(类型是arp_req)里又存着一个链表头，这些二级链表(节点类型是cached_pkt)缓存着相同目标ip但不知道mac地址的包
void *arpcache_sweep(void *arg) 
{
	while (1) {
		sleep(1);
		//assert(0 && "TODO: function arpcache_sweep not implemented!");
		// 加锁以保护ARP缓存表和请求列表
        pthread_mutex_lock(&arpcache.lock);

        time_t now = time(NULL);

        // 遍历ARP缓存表，检查是否有超时条目
        for (int i = 0; i < MAX_ARP_SIZE; i++) {
            if (arpcache.entries[i].valid && (now - arpcache.entries[i].added > 15)) {
                // 如果条目已存在超过15秒，将其valid属性置为0
                arpcache.entries[i].valid = 0;
            }
        }

        // 遍历请求列表，处理正在进行的ARP请求
        struct arp_req *req_entry = NULL, *req_q;
        list_for_each_entry_safe(req_entry, req_q, &(arpcache.req_list), list) {
            if (now - req_entry->sent >= 1) {
                if (req_entry->retries >= ARP_REQUEST_MAX_RETRIES) {
                    // 如果重传次数已达5次，发送ICMP错误包并丢弃所有待处理数据包

                    // 保存需要发送ICMP包的信息
                    int packet_count = 0;
                    struct cached_pkt *pkt_entry = NULL, *pkt_q;
                    list_for_each_entry_safe(pkt_entry, pkt_q, &(req_entry->cached_packets), list) {
                        packet_count++;
                    }

                    char **packets = (char **)malloc(packet_count * sizeof(char *));
                    int *packet_lens = (int *)malloc(packet_count * sizeof(int));
                    int idx = 0;

                    list_for_each_entry_safe(pkt_entry, pkt_q, &(req_entry->cached_packets), list) {
                        packets[idx] = pkt_entry->packet;
                        packet_lens[idx] = pkt_entry->len;

                        // 删除缓存的包
                        list_delete_entry(&(pkt_entry->list));
                        free(pkt_entry);
                        idx++;
                    }

                    // 删除ARP请求项
                    list_delete_entry(&(req_entry->list));
                    free(req_entry);

                    // 解锁以避免死锁
                    pthread_mutex_unlock(&arpcache.lock);

                    // 发送所有ICMP错误包
                    for (int i = 0; i < packet_count; i++) {
                        icmp_send_packet(packets[i], packet_lens[i], ICMP_DEST_UNREACH, ICMP_HOST_UNREACH);
                        free(packets[i]);
                    }

                    // 释放临时数组
                    free(packets);
                    free(packet_lens);

                    // 重新加锁继续处理其他项
                    pthread_mutex_lock(&arpcache.lock);
                } else {
                    // 如果未达到重传次数限制，重传ARP请求
                    arp_send_request(req_entry->iface, req_entry->ip4);
                    req_entry->retries++;
                    req_entry->sent = now; // 更新发送时间
                }
            }
		}
        // 解锁
        pthread_mutex_unlock(&arpcache.lock);
	}

	return NULL;

//检查完毕，修改了已经有5次的发送方式，加了解锁和重加锁
}
