#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>

// Don't include these in another file.
#include "thread.h"
#include "thread-sync.h"

#define BUFFER_SIZE 4096
#define MAX_PATH_LENGTH 1024
#define DEFAULT_PORT 8080
#define MOST_CONNECTIONS 4 // 最大并发连接数


#include <pthread.h>
int global_seq = 0;         // 全局请求序号（分配用）
int next_log_seq = 0;       // 下一个应输出日志的序号
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t log_cond = PTHREAD_COND_INITIALIZER;
__thread int my_seq;  

sem_t sem; // 信号量，用于限制并发连接数
int client_sockets[MOST_CONNECTIONS]; // 存储客户端套接字

typedef struct{
    int seq; //请求序号
    char method[16]; //GET/POST
    char path[MAX_PATH_LENGTH];
    int status_code //200/404/500
}log_entry_t;


//并行处理写了，但没有按照到来顺序打印
//n_问题不知道怎么解决，这里join会在main函数结束时自动调用，可能没有影响？
void thread_worker(int id) {
    int client_socket = client_sockets[id - 1];


    pthread_mutex_lock(&log_mutex);
    my_seq = global_seq++;
    pthread_mutex_unlock(&log_mutex);
    handle_request(client_socket);
    V(&sem); // 处理完释放一个名额
}


// Revise this.
void handle_request(int client_socket);

// Call this.
void log_request(const char *method, const char *path, int status_code);

int main(int argc, char *argv[]) {
    // Socket variables
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    // Get port from command line or use default
    int port = (argc > 1) ? atoi(argv[1]) : DEFAULT_PORT;

    // Set up signal handler for SIGPIPE to prevent crashes
    // when client disconnects
    signal(SIGPIPE, SIG_IGN);

    // Create socket
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options to reuse address
    // (prevents "Address already in use" errors)
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;         // IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY; // Accept connections on any interface
    server_addr.sin_port = htons(port);       // Convert port to network byte order

    // Bind socket to address and port
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections with system-defined maximum backlog
    if (listen(server_socket, SOMAXCONN) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", port);


    SEM_INIT(&sem, MOST_CONNECTIONS); // Initialize semaphore with maximum connections

    // Main server loop - accept and process connections indefinitely
    while (1) {
        P(&sem); // Wait for a slot to be available

        // Accept new client connection
        if ((client_socket = accept(server_socket,
                                    (struct sockaddr *)&client_addr,
                                    &client_len)) < 0) {
            perror("Accept failed");
            V(&sem); // Release the semaphore slot
            continue;  // Continue listening for other connections
        }
        client_sockets[n_] = client_socket; 
        spawn((void*)thread_worker); // Create a new thread to handle the request


        // // Set timeouts to prevent hanging on slow or dead connections
        // struct timeval timeout;
        // timeout.tv_sec = 30;  // 30 seconds timeout
        // timeout.tv_usec = 0;
        // setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO,
        //            (const char*)&timeout, sizeof(timeout));
        // setsockopt(client_socket, SOL_SOCKET, SO_SNDTIMEO,
        //            (const char*)&timeout, sizeof(timeout));

        // // Process the client request
        // handle_request(client_socket);
    }

    // Clean up (note: this code is never reached in this example)
    close(server_socket);
    return 0;
}

int parse_request(const char* buffer, char* method, char* path, char *query_string){
    // GET /cgi-bin/hello?name=world

    //读取GET
    const char *sp1 = strchr(buffer, ' ');
    if (!sp1) return -1;
    size_t mlen = sp1 - buffer;
    strncpy(method, buffer, mlen);
    method[mlen] = '\0';

    //读取/cgi-bin/hello?name=world
    const char *sp2 = strchr(sp1 + 1, ' ');
    if (!sp2) return -1;
    size_t plen = sp2 - (sp1 + 1);
    strncpy(path, sp1 + 1, plen);
    path[plen] = '\0';

    //截出path和query_string
    char *q = strchr(path, '?');
    if (q) {
        *q = '\0';
        strcpy(query_string, q + 1);
    } else {
        query_string[0] = '\0';
    }
    return 0;
}

void process_cgi(int client_socket, const char *path, const char *query_string, const char *method, int *status_code) {
    
    //将 URL 路径转换为文件系统路径
    char script_path[MAX_PATH_LENGTH];
    snprintf(script_path, sizeof(script_path), ".%s", path); // path 已经是 /cgi-bin/xxx

    struct stat st;

    //文件是否存在
    if (stat(script_path, &st) < 0) {
        *status_code = 404;
        const char *resp = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        send(client_socket, resp, strlen(resp), 0);
        return;
    }

    //文件是否可执行
    if (!(st.st_mode & S_IXUSR)) {
        *status_code = 500;
        const char *resp = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n";
        send(client_socket, resp, strlen(resp), 0);
        return;
    }

    //创建管道
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        *status_code = 500;
        const char *resp = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n";
        send(client_socket, resp, strlen(resp), 0);
        return;
    }

    //创建子进程
    pid_t pid = fork();
    if (pid < 0) {//fork失败，关闭管道，返回
        close(pipefd[0]); close(pipefd[1]);
        *status_code = 500;
        const char *resp = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n";
        send(client_socket, resp, strlen(resp), 0);
        return;
    }
    if (pid == 0) {
        // 子进程
        close(pipefd[0]);//关闭读口
        dup2(pipefd[1], STDOUT_FILENO);//重定向标准输出到管道
        close(pipefd[1]);//关闭原写口

        setenv("REQUEST_METHOD", method, 1);
        setenv("QUERY_STRING", query_string, 1);

        //执行cgi脚本
        execl(script_path, script_path, NULL);
        exit(127);
    } else {
        // 父进程
        close(pipefd[1]);//关闭写口

//curl一直异常缺失最后两行，修改echo后解决了，应该是content-length问题

        char buf[BUFFER_SIZE];//读到buf中
        ssize_t n;

//一直没搞清楚返回什么状态码，应该返回echo "HTTP/1.1 403 OK" 这一句里的状态码
        int header_parsed = 0;
        int temp_status = 200; // 默认200
        char header_buf[BUFFER_SIZE * 2] = {0};
        size_t header_len = 0;

        // 先读取头部
        while (!header_parsed && (n = read(pipefd[0], buf, sizeof(buf))) > 0) {
            if (header_len + n < sizeof(header_buf)) {
                memcpy(header_buf + header_len, buf, n);
                header_len += n;
                header_buf[header_len] = '\0';
            } else {
                break;
            }
            // 查找头部结束（\r\n\r\n 或 \n\n）
            char *header_end = strstr(header_buf, "\r\n\r\n");
            int header_size = 0;
            if (!header_end) {
                header_end = strstr(header_buf, "\n\n");
                if (header_end) header_size = header_end - header_buf + 2;
            } else {
                header_size = header_end - header_buf + 4;
            }
            if (header_end) {
                // 解析Status或HTTP/1.1
                    // 查找HTTP/1.1
                char *http_line = strstr(header_buf, "HTTP/1.1");
                if (http_line) {
                    int code = 0;
                    sscanf(http_line, "HTTP/1.1 %d", &code);
                    printf("HTTP code: %d\n", code);
                    if (code >= 100 && code <= 599) temp_status = code;
                }
                // 先把头部发给客户端
                send(client_socket, header_buf, header_size, 0);
                // 剩余body部分
                if (header_len > header_size) {
                    send(client_socket, header_buf + header_size, header_len - header_size, 0);
                }
                header_parsed = 1;
            }
        }
        
        // 继续转发剩余内容
        while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
            send(client_socket, buf, n, 0);
        }
        close(pipefd[0]);
        int wstatus;
        waitpid(pid, &wstatus, 0);
        *status_code = temp_status;
    }

}

void handle_request(int client_socket) {
    char buffer[BUFFER_SIZE];
    int bytes_received;

    // Read request
    bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received <= 0) {
        close(client_socket);//add this line
        return;
    }
    buffer[bytes_received] = '\0';

    //printf("Got a new request:\n%s\n", buffer);
    char method[16], path[MAX_PATH_LENGTH], query_string[BUFFER_SIZE];
    int parse_result = parse_request(buffer, method, path, query_string);
    int status_code = 200;

    if(parse_result < 0) {
        // Invalid request format
        status_code = 400;
        const char *resp = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
        send(client_socket, resp, strlen(resp), 0);
    }else if (strncmp(path, "/cgi-bin/", 9) == 0) {
        process_cgi(client_socket, path, query_string, method, &status_code);
    } else {
        const char *body = "Under construction";
        char header[128];
        snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n",
            strlen(body));
        send(client_socket, header, strlen(header), 0);
        send(client_socket, body, strlen(body), 0);
        status_code = 200;
    }

    //log_request(method, path, status_code);
    pthread_mutex_lock(&log_mutex);
    while (my_seq != next_log_seq) {
        pthread_cond_wait(&log_cond, &log_mutex);
    }
    log_request(method, path, status_code);
    next_log_seq++;
    pthread_cond_broadcast(&log_cond);
    pthread_mutex_unlock(&log_mutex);

    // Close the connection
    close(client_socket);
}


void log_request(const char *method, const char *path, int status_code) {
    time_t now;
    struct tm *tm_info;
    char timestamp[26];

    time(&now);
    tm_info = localtime(&now);
    strftime(timestamp, 26, "%Y-%m-%d %H:%M:%S", tm_info);

    // In real systems, we write to a log file,
    // like /var/log/nginx/access.log
    printf("[%s] [%s] [%s] [%d]\n", timestamp, method, path, status_code);
    fflush(stdout);
}
