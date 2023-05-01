
#ifndef _HEADER_H_
#define _HEADER_H_

#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "linked_list.h"
#define MAX_CONNS 100
#define TIMEOUT 5000
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

typedef struct
{
    char topic[MAX_TOPIC_SIZE];
    uint8_t data_type;
    char data[MAX_CONTENT_SIZE];
    struct sockaddr_in *addr;
    linked_list_t *subscribers;
    int delivered;
} __attribute__((__packed__)) topic;

typedef struct
{
    char *id;
    int socket;
    int status;
    int sf;
    linked_list_t *topics;

} __attribute__((__packed__)) tcp_client;

typedef struct
{
    char topic[MAX_TOPIC_SIZE];
    uint8_t data_type;
    char data[MAX_CONTENT_SIZE];
} __attribute__((__packed__)) message;

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
