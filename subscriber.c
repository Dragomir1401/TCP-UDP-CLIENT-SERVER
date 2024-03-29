#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include "utils.h"
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <sys/eventfd.h>
#include <sys/sendfile.h>

void prepare_conn(int argc, char *argv[], int *tcp_socket, int *epollfd, int *eventfd)
{
    // Check correct usage
    DIE(argc < 4, "You need this usage syntax: %s client_id server_address server_port.", argv[0]);

    // Turning off buffering at stdout
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    // Read socket
    *tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    DIE(*tcp_socket < 0, "Unable to read TCP socket.");

    // Set up server info
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[3]));
    int rc = inet_aton(argv[2], &serv_addr.sin_addr);
    DIE(rc == 0, "Unable to convert server address.");

    // Connect with server
    rc = connect(*tcp_socket, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    DIE(rc < 0, "Unable to connect to server.");

    // Turn off Nagle algorithm
    int yes = 1;
    rc = setsockopt(*tcp_socket, IPPROTO_TCP, TCP_NODELAY, (char *)&yes, sizeof(int));
    DIE(rc < 0, "Unable to disable Nagle algorithm.");

    // Send client ID to the server
    char *buf = malloc(MAX_ID_SIZE);
    strncpy(buf, argv[1], strlen(argv[1]) + 1);
    rc = send(*tcp_socket, buf, strlen(buf), 0);
    DIE(rc < 0, "Unable to send TCP client id to server.");

    // Create epoll
    *epollfd = epoll_create1(0);
    DIE(*epollfd < 0, "Unable to create epoll.");

    // Add TCP socket to epoll
    struct epoll_event tcp_event;
    tcp_event.data.fd = *tcp_socket;
    tcp_event.events = EPOLLIN;
    epoll_ctl(*epollfd, EPOLL_CTL_ADD, *tcp_socket, &tcp_event);
    DIE(rc < 0, "Unable to add TCP socket to epoll instance.");

    // Add stdin to epoll to see if we get commands from input
    struct epoll_event stdin_event;
    stdin_event.data.fd = STDIN_FILENO;
    stdin_event.events = EPOLLIN;
    epoll_ctl(*epollfd, EPOLL_CTL_ADD, STDIN_FILENO, &stdin_event);
    DIE(rc < 0, "Unable to add stdin socket to epoll instance.");
}

int recv_all(int sockfd, char *buffer, size_t len)
{
    int size;
    int rc = recv(sockfd, &size, sizeof(int), 0);
    DIE(rc < 0, "Unable to receive size of message.");

    size_t bytes_received = 0;
    size_t bytes_remaining = size;
    char *buff = buffer;
    while (bytes_remaining)
    {
        // TODO: Make the magic happen
        int rc = recv(sockfd, buff + bytes_received, bytes_remaining, 0);
        // rc == 0 -> conn closed
        // rc < 0 -> error
        // rc = no of bytes recieved

        if (rc < 0)
        {
            printf("Unable to recv.\n");
            break;
        }

        bytes_received += rc;
        bytes_remaining -= rc;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    int tcp_socket, epollfd, eventfd;
    prepare_conn(argc, argv, &tcp_socket, &epollfd, &eventfd);

    int read_size = sizeof(int);
    int recieved_packets = 0;
    while (1)
    {
        struct epoll_event events[MAX_CONNS];
        int num_events = epoll_wait(epollfd, events, MAX_CONNS, -1);
        DIE(num_events < 0, "Epoll wait error.");

        for (int i = 0; i < num_events; i++)
        {
            if (events[i].data.fd == STDIN_FILENO)
            {
                // Stdin reply
                char *buf = malloc(MAX_SIZE);
                memset(buf, 0, MAX_SIZE);
                int rc = read(STDIN_FILENO, buf, MAX_SIZE);
                DIE(rc == -1, "Unable to read from stdin.");

                // Check to see if we need to exit the server
                if (!strncmp(buf, "exit", EXIT_SIZE))
                {
                    fprintf(stderr, "Subscriber sends %s", buf);

                    // Send subscribe buffer to server
                    send(tcp_socket, buf, strlen(buf), 0);

                    printf("Received %d packets.\n", recieved_packets);

                    // Close TCP socket
                    close(tcp_socket);
                    // Exit loop
                    exit(1);
                }
                else if (!strncmp(buf, "subscribe", SUBSCRIBE_COMMAND_LEN))
                {
                    fprintf(stderr, "Subscriber sends %s", buf);
                    printf("Subscribed to topic.\n");

                    // Send subscribe buffer to server
                    send(tcp_socket, buf, strlen(buf), 0);
                }

                else if (!strncmp(buf, "unsubscribe", UNSUBSCRIBE_COMMAND_LEN))
                {
                    fprintf(stderr, "Subscriber sends %s", buf);
                    printf("Unsubscribed from topic.\n");

                    // Send unsubscribe buffer to server
                    send(tcp_socket, buf, strlen(buf), 0);
                }
                else
                {
                    printf("Invalid command. Please consider reading documentation.\n");
                }

                free(buf);
            }
            else if (events[i].data.fd == tcp_socket)
            {
                char *buf = malloc(BUFSIZ);
                // char *size_buf = malloc(BUFSIZ);
                memset(buf, 0, BUFSIZ);
                // memset(size_buf, 0, BUFSIZ);

                int rc = recv_all(tcp_socket, buf, read_size);
                DIE(rc < 0, "Unable to read from TCP socket.");

                if (!strncmp(buf, "close", CLOSE_MESSAGE_LEN))
                {
                    return 0;
                }

                // if (read_size == sizeof(int))
                // {
                //     read_size = atoi(buf);
                //     fprintf(stderr, "Incoming message of size %d.\n", read_size);
                //     continue;
                // }

                if (!rc)
                {
                    printf("%s\n", buf);
                    read_size = sizeof(int);
                    recieved_packets++;
                }

                // fprintf(stderr, "Incoming packet of size: %d\n", ntohl(atoi(size_buf)));

                // int read_size = 0;
                // rc = 0;
                // do
                // {
                //     rc = recv(tcp_socket, buf + read_size, dim_size, 0);
                //     read_size += rc;
                //     // fprintf(stderr, "Read size: %d\n", read_size);
                //     // fprintf(stderr, "Subscriber recieved %s.\n", buf + read_size);
                // } while (read_size <= atoi(size_buf));

                // rc = recv(tcp_socket, buf, ntohl(atoi(size_buf)), 0);
                // if (!strncmp(buf, "close", CLOSE_MESSAGE_LEN))
                // {
                //     return 0;
                // }

                // if (rc == 0)
                // {
                //     fprintf(stderr, "Server closed connection.\n");
                //     close(tcp_socket);
                //     exit(1);
                //     continue;
                // }

                // recieved_packets++;
                // printf("%s\n", buf);

                free(buf);
                // free(size_buf);
            }
        }
    }

    close(tcp_socket);

    return 0;
}
