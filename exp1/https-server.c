#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <malloc.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <resolv.h>
#include "openssl/ssl.h"
#include "openssl/err.h"

#define HTTP_PORT 80
#define HTTPS_PORT 443
#define MAX_REQ_SIZE 4096
#define response1 "HTTP/1.1 301 Moved Permanently\r\nLocation: https://10.0.0.1%s\r\nContent-Length: 0\r\n\r\n"
#define response2 "HTTP/1.0 206 Partial Content\r\nContent-Length: %ld\r\nContent-Range: bytes %ld-%ld/%ld\r\n\r\n"
#define response3 "HTTP/1.0 200 OK\r\nContent-Length: %ld\r\n\r\n"
#define response4 "HTTP/1.0 404 Not Found\r\nContent-Length: 13\r\n\r\n404 Not Found"

// 一个套接字，端口号加上下文
typedef struct {
    int port;
    SSL_CTX* ssl_ctx;
} ServerArgs;

//http请求，80端口
void handle_http_request(int client_sock) {
    
    char request[MAX_REQ_SIZE];

    //请求头，读入request
    ssize_t bytes = read(client_sock, request, sizeof(request)-1);
    if (bytes <= 0) {
        close(client_sock);
        return;
    }
    request[bytes] = '\0';

    // 解析Host
    char host[256] = {0};
    char* host_start = strstr(request, "Host: ");
    if (host_start) {
        sscanf(host_start, "Host: %255[^\r\n]", host);
    }

    // 解析请求路径
    char path[256];
    char version[16];
    sscanf(request, "GET %s %s", path, version);
  
    char response[1024];
    snprintf(response, sizeof(response), response1, path);
    send(client_sock, response, strlen(response), 0);
    close(client_sock);
}




//处理https请求，443端口
void handle_https_request(SSL* ssl) {
 
    char request[MAX_REQ_SIZE];

    // 读取请求数据
    ssize_t bytes = SSL_read(ssl, request, sizeof(request)-1);
    if (bytes <= 0) {
        perror("SSL_read failed");
		exit(1);
    }
    request[bytes] = '\0';

    // 解析请求路径
    char path[256];
    char version[16];
    sscanf(request, "GET %s %s", path,version);

    FILE* fp = fopen(path + 1, "r");

    // 文件不存在，404

    if (fp == NULL) {
        SSL_write(ssl, response4, strlen(response4));
        return;
    }

    // 处理Range请求，206或者200

    fseek(fp,0,SEEK_END);
    long file_size = ftell(fp);
    fseek(fp,0,SEEK_SET);

    long start = 0, end = file_size - 1;
    char* range_hdr = strstr(request, "Range: ");
    long partial_size = 0;

    if (range_hdr) {
        sscanf(range_hdr, "Range: bytes=%ld-%ld", &start, &end);
        partial_size = end - start + 1;
        fseek(fp, start, SEEK_SET);
        char response[256];
        snprintf(response, sizeof(response), response2, partial_size, start, end, file_size);
        SSL_write(ssl, response, strlen(response));
    } else {
        // 发送完整文件，200
        char response[256];
        snprintf(response, sizeof(response), response3, file_size);
        SSL_write(ssl, response, strlen(response));
    }
    char file_buf[1024];
    int read_bytes;
    while ((read_bytes = fread(file_buf, 1, sizeof(file_buf), fp)) > 0) {
        if (range_hdr) {
        // 部分内容
            if (partial_size <= 0) break;
            SSL_write(ssl, file_buf, read_bytes < partial_size ? read_bytes : partial_size);
            partial_size -= read_bytes;
        } else {
        // 完整文件直接发送
        SSL_write(ssl, file_buf, read_bytes);
        }
    }
}



//监听线程函数
void* listener_thread(void* arg) {
    ServerArgs* args = (ServerArgs*)arg;
    int is_https = (args->port == HTTPS_PORT);//区分端口类型

    // 创建套接字
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket() failed");
        return NULL;
    }

    // 设置端口复用
    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse))<0) {
        perror("setsockopt() failed");
        close(sock);
        return NULL;
    }

    // 绑定地址
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(args->port)
    };

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind() failed");
        close(sock);
        return NULL;
    }

    // 开始监听
    if (listen(sock, 10) < 0) {
        perror("listen() failed");
        close(sock);
        return NULL;
    }

    // 循环，等待请求
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_sock = accept(sock, (struct sockaddr*)&client_addr, &addr_len);

        if (client_sock < 0) {
            perror("accept() failed");
            continue;
        }
        if (is_https) {
            // HTTPS处理
            SSL* ssl = SSL_new(args->ssl_ctx);
            SSL_set_fd(ssl, client_sock);

            // SSL握手
            if (SSL_accept(ssl) <= 0) {
                ERR_print_errors_fp(stderr);
                SSL_free(ssl);
                close(client_sock);
                continue;
            }
            handle_https_request(ssl);

            // 清理SSL
            SSL_shutdown(ssl);
            SSL_free(ssl);
        } else {
            // HTTP处理
            handle_http_request(client_sock);
        }
        close(client_sock);
    }

    close(sock);
    return NULL;
}



int main() {

    //初始化SSL库
	SSL_library_init();
	OpenSSL_add_all_algorithms();
	SSL_load_error_strings();

	//创建SSL上下文
	const SSL_METHOD *method = TLS_server_method();
	SSL_CTX *ctx = SSL_CTX_new(method);
    //上下文创建失败
    if (!ctx) {
        fprintf(stderr, "SSL_CTX_new() failed:\n");
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

	//加载证书私钥
	if (SSL_CTX_use_certificate_file(ctx, "./keys/cnlab.cert", SSL_FILETYPE_PEM) <= 0) {
		perror("load cert failed");
		exit(1);
	}
	if (SSL_CTX_use_PrivateKey_file(ctx, "./keys/cnlab.prikey", SSL_FILETYPE_PEM) <= 0) {
		perror("load prikey failed");
		exit(1);
	}


    // 创建两个监听线程t1，t2
    pthread_t t1, t2;
    ServerArgs http_args = {HTTP_PORT, NULL};//80端口，http无加密
    ServerArgs https_args = {HTTPS_PORT, ctx};//443端口，https有ssl加密

    pthread_create(&t1, NULL, listener_thread, &http_args);
    pthread_create(&t2, NULL, listener_thread, &https_args);

    //等待线程结束
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    
    //清理资源
    SSL_CTX_free(ctx);
    return 0;
}