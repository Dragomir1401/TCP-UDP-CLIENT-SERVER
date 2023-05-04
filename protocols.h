
#ifndef _HEADER_H_
#define _HEADER_H_
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define MAX_CONNS 100
#define TIMEOUT 100
#define MAX_SIZE BUFSIZ
#define MAX_COMMAND_SIZE 100
#define MAX_TOPICS_NO 5000
#define MAX_TOPIC_SIZE 50
#define SUBSCRIBE_COMMAND_LEN 9
#define UNSUBSCRIBE_COMMAND_LEN 11
#define MAX_CONTENT_SIZE 1500
#define DATA_TYPE_SIZE 1
#define EXIT_SIZE 4
#define MAX_ID_SIZE 50
#define MAX_SF_SIZE 1
#define CLOSE_MESSAGE_LEN 5
#include <iostream>
#include <vector>
#include <unordered_map>
#include <queue>
using namespace std;

#pragma pack(1)
typedef struct message
{
    int dim;
    string ip_udp;
    string port_udp;
    string topic;
    string data_type;
    string payload;
} message;
#pragma pack(0)

typedef struct tcp_client
{
    char id[MAX_CONNS];
    int socket;
    int topics_count;
    bool active;
    unordered_map<string, int> topics;
} tcp_client;

struct sockaddr_in set_up_server_addr(char *port);
void prepare_conn(int *udp_socket, int *tcp_socket,
                  struct sockaddr_in server_addr, int argc, char *argv[], int *epollfd, int *eventfd);
void add_epoll_events(int epollfd, int tcp_socket, int udp_socket);

#define DIE(condition, message, ...)                                                         \
    do                                                                                       \
    {                                                                                        \
        if ((condition))                                                                     \
        {                                                                                    \
            fprintf(stderr, "[(%s:%d)]: " #message "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
            perror("");                                                                      \
            exit(1);                                                                         \
        }                                                                                    \
    } while (0)

#endif /* _HEADER_H_ */
