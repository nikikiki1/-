实验三：网络传输实验（可靠传输）
1 实验目标
在实验二已完成代码的基础上

实现可靠传输机制
在有丢包的网络拓扑上实现文件传输
2 相关资料
2.1 附件文件列表
基于已经完成的实验二的代码，添加云盘中的文件，最终目录为：


exp3/
├─ bulk.py                  # 实现了文件传输功能的python脚本
├─ tcp_topo_loss.py         # 有丢包的mininet测试拓扑
├─ test.py                  # 测试脚本
├─ pyarmor_runtime_000000   # 测试脚本依赖文件
├─ ...
├─ main.c                   # main.c 等实验二的文件
2.2 抓包调试技巧 (tcpdump & WireShark)
2.2.1 tcpdump
tcpdump 是一款强大的网络抓包工具，它内置在常见的Linux发行版中。由于程序运行在mininet中，以及网络的异步特性，我们可以依赖的调试手段并不多，gdb或者vscode的单步调试功能都不便使用。除了在代码中添加详细的log，抓包是另一种我们可以使用的调试方法。

tcpdump 参数很多，详细教程可以查阅相关博客或者使用man tcpdump命令查看文档。在本次实验中我们使用如下命令便足够了，它会将经过当前机器的报文存储到dump.pcap中：


tcpdump -i h1-eth0 -s0 -U -w dump.pcap
参数解释：

-i h1-eth0：抓取h1-eth0网卡的报文，h1-eth0这个网卡名可以通过mininet内dump命令查看，一般h1的网卡就叫h1-eth0。
-s0：不对抓到的报文进行截断（默认只保留96字节）
-U：不缓冲，抓到包直接写入输出文件
-w dump.pcap：将抓到的报文保存到dump.pcap中
2.2.2 WireShark
Wireshark 是全球最先进、使用最广泛的网络协议分析器，我们可以使用它从微观层面分析网络行为。WireShark 具有图形界面，虽然功能强大，但上手很简单。相较于冗长细致的官方文档，我们更推荐用一些博客学习使用它，在实战节中我们会介绍一些基本操作。

安装：

WireShark不一定安装在实验机器上，可以将其安装在本机，然后将tcpdump抓取的报文下载到本机进行分析。

Windows/Mac：官网下载
Ubuntu: sudo apt install wireshark
2.2.3 演示
实验附件中提供了bulk.py，为已经写好的本次实验功能的python版本。在本节中，我们将分析bulk.py server-client互相发送报文的行为。

Step 1: 抓包

在实验机器上：

生成要发送的文件：./create_randfile.sh
启动mininet：sudo python3 tcp_topo.py 并使用dump查看h2的pid
在mininet的交互界面：
后台启动抓包：h1 tcpdump -i h1-eth0 -s0 -U -w dump.pcap &
启动服务端：h1 python3 bulk.py server 10001
（在运行结束后）关闭抓包：h1 pkill tcpdump
在另一个终端：
设置h2的pid：export PID=<刚查询的pid>
启动客户端：sudo mnexec -a $PID python3 bulk.py client 10.0.0.1 10001
image-20250313205511600

可以看到两边的python脚本都正常结束了，并且在当前目录生成了dump.pcap，将该文件下载到安装了WireShark的机器上。

Step 2: 分析

在安装了WireShark的机器上：

双击打开文件，或者在WireShark主界面后在工具栏点击 文件-打开。呈现出如下界面：

image-20250313210610905

点击关心的报文，在左下角的窗格查看报文头的字段信息，例如TCP Header中的flags, seq, ack_no等信息。

ARP报文与本次实验无关。我们主要关注TCP报文展现出的三个阶段：连接建立、数据传输与连接关闭。

连接建立：

三次握手：

image-20250313211012854

数据传输：

列表中Info列展示了报文的主要信息，例如PSH报文的序列号（Seq）、ACK报文的应答序号（Ack）、接收方窗口大小（Win）。

这里的序列号是相对值，在上次实验中我们已经知道，socket创建时会随机选择一个初始序列号iss，后续的序列号会在此基础上递增，这里的相对序列号就是用报文TCP Header中的实际值减去初始值。实际的序列号可以在左下角的详情窗口查看。如果想要在Info列中查看实际序列号，可以打开 编辑-首选项-Protocol-TCP，取消勾选Relative sequence number。

Snipaste_2025-03-13_21-34-11

右键任意TCP报文，追踪这条流的数据，可以看到传递的全部数据：

image-20250313210910516

Snipaste_2025-03-13_21-13-09

在数据报文的中间尤其需要关注被标识为醒目颜色的报文（比如黑色、红色），这些报文代表了流的值得注意的行为。WireShark会通过seq、ack等信息分析这一TCP流是否按照预期运行，并关注报文乱序、窗口已满等可能导致异常的现象。对照这些异常报文和程序输出的日志，可以让程序调试事半功倍。

接收窗口耗尽：

Snipaste_2025-03-13_21-22-25

丢包（造成序列号不连续，即方括号中说的前一个段未找到）：

Snipaste_2025-03-19_21-07-42

重复ACK（丢包会引发后续的一串ACK都是重复的。这也是课上学习的拥塞控制算法监控的拥塞信号之一：三次重复ACK预示着丢包）：

Snipaste_2025-03-19_21-09-08

还有很多花哨的功能帮助分析TCP流量，比如统计栏（Statistics）中的流量图（Flow Graph)等，可以自行研究。

连接关闭：

注意主动关闭连接的一段发送报文的flags是FIN|ACK而不只是FIN。如果你的C代码不这样编写，可能无法被Python端识别，导致无法完成四次挥手，有关这一现象可以参见这一讨论。

Snipaste_2025-03-13_21-14-41

2.3 参考资料
RFC (Request for Comments)是计算机网络领域中最权威的协议标准参考，与TCP相关的RFC相当繁多，这也linux协议栈的代码如此繁杂难以看懂的原因之一，所以想要实现完整的TCP栈对于我们来说不可能。在我们的实验中，运行的正确性是最重要的评判标准，遵循RFC能使我们的程序相对正确和稳健。如果你对实现的具体方式感到困惑，或者想了解更标准的协议栈实现，请阅读RFC。

这里列出一些与实验三相关的RFC资料，更多的可以问AI：

RFC 793: Transmission Control Protocol：初版TCP标准
RFC 9293: Transmission Control Protocol (TCP)：更新版TCP标准。两个TCP标准是最重要的参考，包含了大多数行为的解释。有一些具体行为阐述不一定详细，需要查询其它RFC。
RFC 6298: Computing TCP's Retransmission Timer：重传定时器行为
RFC 1122: Requirements for Internet Hosts - Communication Layers：4.2节中有很多详细的TCP行为参考，例如4.2.2.17 Probing Zero Windows讲述了收到一个RWND=0的ACK该怎么处理，这与后面Persist Timer实验内容相关。
3 实验内容
3.0 实验前的准备
在实验目录和scripts子目录下执行chmod +x *.sh。为所有.sh脚本赋予执行权限
./create_randfile.sh执行脚本，生成本次实验中用于传输的文件client-input.dat
3.1 滑动窗口
3.1.1 锁机制
在这套系统中，运行着多个线程：

协议栈（ustack_run)：运行在主线程中，主要实现协议栈收包功能，通过一系列调用最终调用到了tcp_process函数。
Timer（tcp_timer_thread）：运行在子线程，实现TCP协议中和时间相关的内容（超时等机制）
用户程序（tcp_server或tcp_client）：运行在子线程，实现TCP应用。它在调用tcp_sock_write时会进行协议栈发包操作。
p.s. 可以将tcp_sock_write理解为linux中的系统调用，调用tcp_sock_write函数发生在“用户态”，具体执行发生在“内核态”。仅仅方便理解，与后续实验关系不大。
这些线程可能会同时读写一些数据，以struct tcp_sock结构体为例：

snd_una, snd_nxt等滑动窗口相关数据会在协议栈数据收发时被同时读写
rcv_buf接收buffer会被协议栈收包tcp_process、TCP应用收包tcp_sock_read同时读写
send_buf发送buffer会被协议栈收发包、Timer线程同时读写
为了保证并发安全性，需要进行上锁。越细粒度的锁性能高，但写起来也更容易出错。这里采用三个粗粒度的锁保护struct tcp_sock，框架中没有，需要手动创建在结构体中。

pthread_mutex_t sk_lock; 保护snd_una等核心参数
收包时：在tcp_process调用之前上锁，之后解锁。这样整个收包过程对socket参数的更改都是安全的。
发包时：在tcp_send_packet调用之前上锁，之后解锁。这里由于每个人实现不一样，加锁位置不一定一样，比如在调用`tcp_send_packet之前，用tcp_tx_window_test检查发送窗口，那么应该在tcp_tx_window_test之前上锁。
pthread_mutex_t rcv_buf_lock 保护struct ring_buffer *rcv_buf
每次访问rcv_buf时
pthread_mutex_t send_buf_lock;保护struct list_head send_buf;
每次访问send_buf时
到这次实验为止还有一个锁，是保护timer_list的timer_list_lock，在上一个实验中已经做了介绍，如果实现正确这一节就不需要改了。

所有的锁都需要初始化，有两种初始化方案：

定义变量时初始化：pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
后续初始化：pthread_mutex_init(&lock, NULL);
死锁是锁使用不当很容易造成的结果，因而非常建议，在每次调用pthread_mutex_lock和pthread_mutex_unlock前后打一个log，用于在程序卡死时观察log，看看哪一锁尝试获取但一直获取不到。注意在函数的调用链上只有最外层需要上锁，例如tcp_process上好锁后，函数内会调用的handle_tcp_recv_data就不需要上锁了。

上锁还有另一个需要注意的问题：在调用sleep_on之前，一定要释放占有的锁。举一个发送报文的例子：


pthread_mutex_lock(&tsk->sk_lock);
while (!tcp_tx_window_test(tsk)) {
    // 如果发送窗口不足，先释放锁，再等待wait_send信号激活
    pthread_mutex_unlock(&tsk->sk_lock);
    sleep_on(tsk->wait_send);
    pthread_mutex_lock(&tsk->sk_lock);
}
tcp_send_packet(tsk, data_pkt, data_pkt_len);
pthread_mutex_unlock(&tsk->sk_lock);
3.1.2 滑动窗口机制
滑动窗口即课本上讲述的 Go Back N 机制，是TCP流控的基础。这一节讲述怎么在没有丢包的场景下实现发送方滑动窗口，对于有丢包的情况，需要引入下一节的重传机制。

发送方：

Snipaste_2025-03-19_22-19-46

这是课本图3-19，发送方窗口被分成了四个部分，从宏观层次可以看出窗口内报文的分类。下图是RFC 793 Figure 4，具体给出了怎么用tcp_sock结构体中的成员分割窗口。


  Send Sequence Space

                   1         2          3          4
              ----------|----------|----------|----------
                     SND.UNA    SND.NXT    SND.UNA
                                          +SND.WND

        1 - old sequence numbers which have been acknowledged
        2 - sequence numbers of unacknowledged data
        3 - sequence numbers allowed for new data transmission
        4 - future sequence numbers which are not yet allowed

                          Send Sequence Space
需要注意，snd_una等tcp_sock结构体的成员变量是以字节为单位，而非报文为单位，TCP是流传输协议，在它的视角中，消息是一串连续的字节，而非若干个报文。假设某个字节在整个消息中的位置是seq，四个部分的对应关系为：

已经被接收的字节：seq < snd_una
发送了，但为收到ACK的字节：snd_una <= seq < snd_nxt
未发送，且当前发送窗口剩余大小允许继续发送的字节：snd_nxt <= seq < snd_una + snd_wnd
发送窗口剩余大小不允许继续发送的字节：seq >= snd_una + snd_wnd
如何将一个报文和上述字节表示的窗口进行对照呢，假设一个报文初始序列号为seq，长度为len，那么它对应于[seq, seq+len-1]这些字节，如果这些字节都在某个窗口中，那么这个报文就属于这个窗口。如果一个报文一部分处于窗口3，一部分处于窗口4，那么没有足够的剩余窗口，是不可以发送的。

接收方：

接收方采用累计确认机制进行回复，按照课本的GBN机制，接收方是不维护窗口的。不过一般还是会维护乱序队列（Out-of-Order Queue），对应于tcp_sock中的rcv_ofo_buf，这是下一节要完成的内容，本节还是假设没有窗口。

累计确认机制下，接收端发送的ACK报文的应答序号tcp->ack始终是自己希望收到的下一个字节tsk->rcv_nxt，这一点与是否实现乱序队列无关，因而实验中发送ACK报文只需要调用tcp_send_control_packet(tsk, ACK)，序号的设置会由函数自动进行。

在本节中，接收方收到一个报文，需要检查该报文的序号（范围[cb->seq, cb->seq_end)）和自己期望收到的序号tsk->rcv_nxt的关系。注意，比较序列号时最好使用tcp.h中提供的比较函数less_than_32b 等，它们会自动处理整数的溢出问题。

cb->seq_end <= tsk->rcv_nxt：收到了之前确认过的报文，不需要处理报文中的数据，直接回复ACK。这种情况在无丢包时不应该出现，但可以添加这一处理，防止python侧发送这样的报文（例如KeepAlive报文）。
tsk->rcv_nxt == cb->seq：刚好是自己希望收到的报文，将数据上送rcv_buf
tsk->rcv_nxt < cb->seq：乱序报文，后面处理，这里直接回复ACK，数据丢弃。
其它情况不应出现
3.1.3 编写代码
由于每个人在实验二中的实现不一样，这里给出修改方向。

锁机制：

确认tcp_sock相关的三个锁以及timer_list_lock正确使用
滑动窗口：

新增tcp_tx_window_test函数，检查当前的窗口是否能继续发送报文，即空余窗口是否至少为TCP_MSS（TCP Max Segment Size）大小


#define TCP_MSS (ETH_FRAME_LEN - ETHER_HDR_SIZE - IP_BASE_HDR_SIZE - TCP_BASE_HDR_SIZE)

// 使用tsk->snd_una, tsk->snd_wnd, tsk->snd_nxt计算剩余窗口大小，如果大于TCP_MSS，则返回1，否则返回0
int tcp_tx_window_test(struct tcp_sock *tsk)
修改原本的tcp_update_window


/*
1. 记录更新前的tcp_tx_window_test结果
2. 更新snd_una， adv_wnd， snd_wnd。变量含义在tcp_sock结构体注释里。cwnd后面拥塞控制才会用到，可以设置一个较大的值0x7f7f7f7f。
3. 检查新的tcp_tx_window_test结果
4. 如果原本没有足够的窗口，现在有了，唤醒tsk->wait_send
*/
static inline void tcp_update_window(struct tcp_sock *tsk, struct tcp_cb *cb)
更新tcp_process函数

在接收到ACK时调用tcp_update_window_safe。最好snd_una, adv_wnd, snd_wnd这三个值的更新只应该发生在tcp_update_window_safe中，否则可能导致窗口状态判断错误，无法唤醒wait_send信号。
更新接收报文后回复ACK的部分，按照上述机制。
检查与滑动窗口相关的几个状态值是否按照预期正确更新，最好在这几个变量发生变化的地方、以及tcp_tx_window_test之类的重要的判断状态的函数内打上足够的log。

文件传输：

修改tcp_apps.c，使之能够收发文件。具体的逻辑是：Client将client-input.dat中的内容传输给Server，Server将收到的内容存⼊server- output.dat中。

提供的bulk.py中实现了python版的文件传输功能，C程序并不需要实现的完全一样，能进行文件传输，最终保存正确输出文件即可。python代码中的setsockopt用于禁用一些TCP高级特性，无需关心。

3.2 Persist Timer
3.2.1 Zero Window 问题
Persist Timer 用来解决TCP传输中一个经典的问题：当接收方通知接收窗口归零时，发送主机无法再发送任何内容，直到收到新的ACK通告的窗口变为非零值。然而发送方不发送消息，接收方也不会发送ACK。此时，发送方和接收方都不再发送任何报文，发生死锁。

这个问题有两种常见的方案：

Window Update Message：在RFC 793 Section 3.7和RFC 1122 Section 4.2.2.17中给出了一个解决方案，接收方在接收窗口从零变为非零时，主动发送一个ACK（被成为Window Update Message），告知发送方可以继续发送。这一方案存在一个问题，ACK的传输是不可靠的，ACK一旦丢失，死锁问题将会继续存在。
Persist Timer：在RFC 9293 Section 3.8.6.1中给出的 Zero-Window Probing 机制是现在linux等socket都必须实现的解决方案。即使发送方被通知接收窗口归零，发送方仍会隔一段时间发送一个Probe报文，它包含一个字节的数据，用这个数据试探新的接收窗口。
本实验中：

窗口小于TCP_MSS时，发送就停止了（参见上面修改的tcp_update_window函数），所以上面的窗口归零在本实验中被替换为，窗口从大于等于TCP_MSS变为小于TCP_MSS。窗口恢复类同。
你可以选择只实现Window Update Message，不实现Persist Timer，寄希望于测试时Window Update Message刚好不会被丢弃，这样只需要添加几行代码。我们推荐实现Persist Timer，添加起来也并不复杂，至于Window Update Message则非必要。这两个机制至少要实现一个，因为发送窗口归零的概率并不低。
由于本框架没有通常意义上的发送缓冲区（应用层和协议栈之间的，和上面的send_buf不是一个东西），所以我们无法在协议栈内得知下一个要发送的字节是什么。因而这里对Probe报文稍作修改，修改为发送一个已经ACK过的字节，这样仍然可以正常工作，因为TCP 接收方在接收到一个已经ACK过的字节时，会回复ACK。
3.2.2 Persist Timer 实现
tcp_set_persist_timer：启用persist timer


/*
1. 如果已经启用，则直接退出
2. 创建定时器，设置各个成员变量，设置timeout为比如TCP_RETRANS_INTERVAL_INITIAL
3. 增加tsk的引用计数，将定时器加入timer_list末尾
*/
void tcp_set_persist_timer(struct tcp_sock *tsk);
tcp_unset_persist_timer：禁用persist timer


/*
1. 如果已经禁用，不做任何事
2. 调用free_tcp_sock减少tsk引用计数，并从链表中移除timer
*/
void tcp_unset_persist_timer(struct tcp_sock *tsk);
tcp_send_probe_packet：发送Probe报文


/*
仿照tcp_send_packet函数，发送probe报文。几处改动：
1. 发送的序列号设置为一个已经ACK过的序列号（比如tsk->snd_una - 1）
2. 不需要更新snd_nxt
3. 不需要设置重传相关内容
4. TCP负载为一个任意的字节
*/
void tcp_send_probe_packet(struct tcp_sock *tsk);
tcp_scan_timer_list中增加对Persist Timer超时的处理。如果TCP没有关闭，且发送窗口snd_wnd小于TCP_MSS，则发送一个Probe报文、重置时间，否则关闭该定时器。

修改tcp_update_window函数。比较简单的修改方式是，只要新的snd_wnd小于TCP_MSS，就调用tcp_set_persist_timer，否则调用tcp_unset_persist_timer，这两个函数在已经启用或禁用时不会重复操作。当然也可以加些判断，只在归零和恢复的时候调用这两个函数。

3.2.3 编译测试
编写完以上的代码后，应当可以通过无丢包的文件传输测试。

编译运行方法和实验二一致。修正一下之前的运行方法，因为tcp_topo.py在创建拓扑时已经运行了disable_offloading.sh等几个脚本，后续不需要手动执行了。

测试1：服务端和客户端均为tcp_stack


# 终端1
sudo python3 tcp_topo.py
dump
h1 ./tcp_stack server 10001

# 终端2
export PID=xxxx # xxxx是dump查到的h2的pid
sudo mnexec -a $PID ./tcp_stack client 10.0.0.1 10001
测试2：服务端为bulk.py，客户端为tcp_stack


# 终端1
sudo python3 tcp_topo.py
dump
h1 python3 bulk.py server 10001

# 终端2
export PID=xxxx # xxxx是dump查到的h2的pid
sudo mnexec -a $PID ./tcp_stack client 10.0.0.1 10001
测试3：服务端为tcp_stack，客户端为bulk.py


# 终端1
sudo python3 tcp_topo.py
dump
h1 ./tcp_stack server 10001

# 终端2
export PID=xxxx # xxxx是dump查到的h2的pid
sudo mnexec -a $PID python3 bulk.py client 10.0.0.1 10001
Info

似乎有同学遇到了h2发送有序的数据，但h1收到的报文乱序的问题，可能是内核参数的配置问题，暂时没排查出根因。如果遇到了，可以上传OJ测试，或者做完后续实验再回来测试。

预期结果：

终端执行：


diff client-input.dat server-output.dat
什么都没输出说明两个文件一样。

Snipaste_2025-03-13_21-57-16

如果不一样，可以用vscode的对比功能可视化地看看哪里不一样，例如丢包或者写入文件不及时导致缺失数据。

Snipaste_2025-03-13_21-59-50

Snipaste_2025-03-13_22-00-27

可选：在发送时记录发送时间，测算发送速率，默认参数下发送速率不应低于1MBps，不做强制要求。

3.3 丢包重传
3.3.1 GBN机制
丢包重传是Go Back N机制的一部分，用于丢包恢复。首先回忆课本上的GBN，它的接收端只接收严格按序到达的数据包，如果收到了乱序包，会直接丢弃，在ACK报文的ack号中写上自己希望收到的下一个序号。

这种机制在实际部署时会有严重的性能问题，因为一旦丢包，有大量的报文被浪费了。在linux协议栈等现代TCP实现中，即使是GBN机制，也会维护乱序队列（linux中的out_of_order_queue，实验中的rcv_ofo_buf），接收方会将乱序报文暂存在这个队列中，等待丢包恢复机制生效，丢的包到达后，从乱序队列中取出连续的有效信息上送应用层。

乱序队列在linux中以红黑树方式组织的，可以理解为PriorityQueue，有序队列，在队列中的报文会按照序列号从小到大排序。在我们的实验中，没有引入红黑树这种复杂的数据结构，是简单的list，所以需要手动保证队列的有序性，也即：向 rcv_ofo_buf中插入数据时 ，应当将数据插入到合适的位置，使队列按序列号从小到大排序。将一个元素插入到队列中间采用list.h中提供的函数list_for_each_entry_safe, list_insert。

首先以最概要的方式绍本实验的重传机制，具体机制会在后续介绍：

发送端：发送数据报文、FIN、SYN报文后将报文加入send_buf中，ACK报文到达时会将已经ACK的报文从send_buf中移除，重传定时器超时则会重发第一个报文。
接收端：接收到数据报文后，检查序列号后将报文放入rcv_ofo_buf，然后查看rcv_ofo_buf中是否有连续有效的数据报文，如果有则上送接收缓冲区rcv_buf。
3.3.2 重传定时器
重传定时器的行为参照RFC 6298: Computing TCP's Retransmission Timer：


5.  Managing the RTO Timer

   An implementation MUST manage the retransmission timer(s) in such a
   way that a segment is never retransmitted too early, i.e., less than
   one RTO after the previous transmission of that segment.

   The following is the RECOMMENDED algorithm for managing the
   retransmission timer:

   (5.1) Every time a packet containing data is sent (including a
         retransmission), if the timer is not running, start it running
         so that it will expire after RTO seconds (for the current value
         of RTO).

   (5.2) When all outstanding data has been acknowledged, turn off the
         retransmission timer.

   (5.3) When an ACK is received that acknowledges new data, restart the
         retransmission timer so that it will expire after RTO seconds
         (for the current value of RTO).

   When the retransmission timer expires, do the following:

   (5.4) Retransmit the earliest segment that has not been acknowledged
         by the TCP receiver.

   (5.5) The host MUST set RTO <- RTO * 2 ("back off the timer").  The
         maximum value discussed in (2.5) above may be used to provide
         an upper bound to this doubling operation.

   (5.6) Start the retransmission timer, such that it expires after RTO
         seconds (for the value of RTO after the doubling operation
         outlined in 5.5).

   (5.7) If the timer expires awaiting the ACK of a SYN segment and the
         TCP implementation is using an RTO less than 3 seconds, the RTO
         MUST be re-initialized to 3 seconds when data transmission
         begins (i.e., after the three-way handshake completes).

         This represents a change from the previous version of this
         document [PA00] and is discussed in Appendix A.
具体来说新增如下内容：

struct tcp_timer：添加表示当前累计重传次数的变量

tcp_set_retrans_timer：启用计时器


/*
1. 如果已经启用，则更新超时时间为当前的RTO后退出
2. 创建定时器，设置各个成员变量，初始RTO为TCP_RETRANS_INTERVAL_INITIAL。
3. 增加tsk的引用计数，将定时器加入timer_list末尾
*/
void tcp_set_retrans_timer(struct tcp_sock *tsk)
tcp_unset_retrans_timer：禁用计时器


/*
1. 如果已经禁用，不做任何事
2. 调用free_tcp_sock减少tsk引用计数，并从链表中移除timer
*/
void tcp_unset_retrans_timer(struct tcp_sock *tsk);
tcp_update_retrans_timer：在收到ACK后更新定时器


/*
1. 确认定时器是启用状态
2. 如果发送队列为空，则删除定时器，并且唤醒发送数据的进程。否则重置计时器，包括timeout和重传计数。

注意调用这个函数之前，需要完成对发送队列的更新。
*/
void tcp_update_retrans_timer(struct tcp_sock *tsk);
修改tcp_scan_timer_list：增加对重传计时器的支持。不同的计时器在判断超时上是一样的，只是通过timer->type区分。超时发生时，检查socket未关闭。如果达到重传次数上限（自行设置，比如3），则强制关闭socket（发送RST，用unhash之类的函数将socket从各种绑定的队列中移除，并唤醒wait_connect之类的信号量）。如果未达到重传上限，则调用tcp_retrans_send_buffer（后面做）执行重传，并更新重传次数和timeout时间。

3.3.3 发送队列
本实验中的发送队列存储着发送了、但是尚未应答的报文，用于重传。

所有队列都需要用init_list_head初始化。

tcp_sock.h中新建struct send_buffer_entry：它是send_buf的队列成员，成员为报文和报文长度，创建这个结构时应当拷贝报文信息，而非直接复制指针，防止重复释放。

tcp_send_buffer_add_packet：在发送报文时，将其加入发送队列。


/*
创建send_buffer_entry加入send_buf尾部

注意上锁，后面不再强调。
*/
void tcp_send_buffer_add_packet(struct tcp_sock *tsk, char *packet, int len);
tcp_update_send_buffer：在收到ACK时，更新发送队列。


/*
基于收到的ACK包，遍历发送队列，将已经接收的数据包从队列中移除

提取报文的tcp头可以使用packet_to_tcp_hdr，注意报文中的字段是大端序
*/
int tcp_update_send_buffer(struct tcp_sock *tsk, u32 ack);
tcp_retrans_send_buffer：在重传定时器超时后，重传发送队列的第一个包。


/*
获取重传队列第一个包，修改ack号和checksum并通过ip_send_packet发送。

注意不要更新snd_nxt之类的参数，这是一个独立的重传报文。ip_send_packet会释放传入的指针，因而需要拷贝需要重传的报文。
*/
int tcp_retrans_send_buffer(struct tcp_sock *tsk);
3.3.4 接收队列
新增struct recv_ofo_buf_entry：它是recv_ofo_buf链表的成员。包含报文内容、长度、序列号(seq)、结束序列号（seq_end，类似cb->seq_end）

tcp_recv_ofo_buffer_add_packet：将收到的所有数据包加入乱序队列。


/*
1. 创建recv_ofo_buf_entry
2. 用list_for_each_entry_safe遍历rcv_ofo_buf，将表项插入合适的位置。如果发现了重复数据包，则丢弃当前数据。
3. 调用tcp_move_recv_ofo_buffer执行报文上送
*/
int tcp_recv_ofo_buffer_add_packet(struct tcp_sock *tsk, struct tcp_cb *cb);
tcp_move_recv_ofo_buffer: 将乱序队列中的有序部分上送接收缓冲区


/*
遍历rcv_ofo_buf，将所有有序的（序列号等于tsk->rcv_nxt）的报文送入接收队列（tsk->rcv_buf）
更新rcv_nxt, rcv_wnd并唤醒接收线程(wait_recv)

如果接收队列已满，应当退出函数，而非等待。
*/
int tcp_move_recv_ofo_buffer(struct tcp_sock *tsk);c
3.3.5 更新协议栈代码
在合适的位置调用上面创建的函数：

tcp_set_retrans_timer：和tcp_send_buffer_add_packet绑定。在tcp_send_packet发送数据报文，以及tcp_send_control_packet发送SYN、FIN报文时调用。TCP协议中ACK报文是不需要重传的。
tcp_unset_retrans_timer：在连接建立成功时，即进入TCP_ESTABLISHED时。关闭连接类似，和上面的设置定时器对应。
tcp_update_retrans_timer：数据传输阶段收到ACK时
tcp_send_buffer_add_packet：同tcp_set_retrans_timer
tcp_update_send_buffer：收到ACK时，与tcp_update_retrans_timer不同，还包含了SYN、FIN的ACK。
tcp_retrans_send_buffer：在重传定时器超时时。
tcp_recv_ofo_buffer_add_packet：在接收到数据报文时调用。
tcp_move_recv_ofo_buffer：在tcp_recv_ofo_buffer_add_packet执行最后调用。
3.4 一些特殊情况
3.4.1 Python 侧 socket 的细节行为
由于Python侧的socket是标准的linux协议栈，所以可能有一些细节行为我们上面没有提到。下面说明一些可能遇到的情况，如果遇到其它问题，可以通过抓包分析进行判断。

FIN|PSH合并现象：FIN flag会附加在发送的最后一个数据报文上，也即会出现一个flags为FIN|PSH|ACK的报文，因而在编写代码时，要注意FIN报文也可能携带数据。
FIN报文的处理：在状态机中，当接收方收到携带了FIN的消息时，应当发送ACK并进入CLOSE_WAIT状态。但考虑这样的场景：临近FIN报文的一个数据报文丢包了，此时在FIN之后，仍然还需要收发重传报文。实际上，状态机中所谓的接收到FIN实际上是接收到非乱序的FIN，因而可以采用这样的解决方案：如果收到了携带FIN的报文，而当前的rcv_nxt和这个报文不符，则直接丢弃它，不进行任何处理。这样做，直到所有数据都被接收，携带FIN的报文才会被真正ACK。
不带PSH标志的数据报文：数据报文不一定携带PSH标识，具体原因可以搜索PSH标识的含义。在本实验中意味着可能Python会发来一个只有ACK标识的、携带着数据的报文，应该用cb->pl_len判断报文是否携带数据。
3.4.2 编译测试
在完成了以上内容后，应当可以通过有丢包的文件传输测试

运行方法和上面类似，将tcp_topo.py替换为tcp_topo_loss.py。

预期结果类似，速度最好达到100KBps以上。

4 在线评测说明
4.1 提交格式
请将工程文件直接压缩为zip文件，并上传提交。

MAC用户注意事项同实验二
提交前删除临时文件（如果有需要的文件注意保存到其它地方）：


make clean
sudo rm *.log *.dat *.pcap
4.2 注意事项
直接压缩为zip文件而不是rar或者tar.gz后缀文件。

不要多层嵌套zip（如zip解压之后仍为zip文件），文件大小不要超过5MB。

目录中必须有Makefile文件，而不是makefile，请保证用原始的Makefile可以编译成功提交的作业。

保证生成文件的命令行参数格式符合规范（以下假定可执行文件为exec）：

如果为server模式，格式为./exec sever port
如果为client模式，格式为./exec client server_IP server_port
4.3 测试程序说明
网络拓扑：为两节点拓扑（两个host节点h1, h2直连，参见tcp_topo.py和tcp_topo_loss.py）

传输文件：大小为4MB左右，由create_randfile.sh随机生成。

无丢包场景

测试1: CSendC_test：C程序互发
测试2: CSendCPython_test：C程序做Client，Python程序做Server
测试3: PythonSendC_test：Python程序做Client，C程序做Server
有丢包场景

测试4: CSendCC-loss_test
测试5: CSendPython-loss_test
测试6: PythonSendC-loss_test
预计用时：提交成功后20s - 240s