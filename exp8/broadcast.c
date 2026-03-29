#include "base.h"
#include <stdio.h>

extern ustack_t *instance;

void broadcast_packet(iface_info_t *iface, const char *packet, int len)
{
	    // 遍历所有接口，除了发起广播的接口
    iface_info_t *curr_iface;
    list_for_each_entry(curr_iface, &instance->iface_list, list) {
        if (curr_iface == iface)
            continue; // 跳过本接口

        // 复制数据包
        char *pkt_copy = (char *)malloc(len);
        memcpy(pkt_copy, packet, len);

        // 发送数据包
        iface_send_packet(curr_iface, pkt_copy, len);

    }
}
