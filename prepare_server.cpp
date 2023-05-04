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
#include <netinet/udp.h>
#include <netinet/in.h>
#include <sys/eventfd.h>
#include <sys/sendfile.h>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <queue>
using namespace std;

void add_epoll_events(int epollfd, int tcp_socket, int udp_socket)
{
    // Add TCP EPOLLIN
    struct epoll_event tcp_event = {};
    tcp_event.data.fd = tcp_socket;
    tcp_event.events = EPOLLIN;
    int rc = epoll_ctl(epollfd, EPOLL_CTL_ADD, tcp_socket, &tcp_event);
    DIE(rc < 0, "Unable to add TCP socket to epoll instance.");

    // Add UDP EPOLLIN
    struct epoll_event udp_event = {};
    udp_event.data.fd = udp_socket;
    udp_event.events = EPOLLIN;
    rc = epoll_ctl(epollfd, EPOLL_CTL_ADD, udp_socket, &udp_event);
    DIE(rc < 0, "Unable to add UDP socket to epoll instance.");

    // Add stdin EPOLLIN
    struct epoll_event stdin_event = {};
    stdin_event.data.fd = STDIN_FILENO;
    stdin_event.events = EPOLLIN;
    rc = epoll_ctl(epollfd, EPOLL_CTL_ADD, STDIN_FILENO, &stdin_event);
    DIE(rc < 0, "Unable to add stdin socket to epoll instance.");
}

void prepare_conn(int *udp_socket, int *tcp_socket,
                  struct sockaddr_in server_addr, int argc, char *argv[], int *epollfd, int *eventfd)
{
    // Turning off buffering at stdout
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    // Checking correct usage
    DIE(argc < 2, "You need to use a port: %s server_port.", argv[0]);

    // Check to see if the server port is valid
    DIE(!atoi(argv[1]), "Invalid port number.");

    // Read TCP socket
    *tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    DIE(*tcp_socket < 0, "Unable to read TCP socket.");

    // Turn off Nagle algorithm
    int option = 1;
    int rc = setsockopt(*tcp_socket, IPPROTO_TCP, TCP_NODELAY, (char *)&option, sizeof(int));
    DIE(rc < 0, "Unable to disable Nagle algorithm.");

    // Turn on reuseaddr for tcp socket
    option = 1;
    if (setsockopt(*tcp_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&option, sizeof(int)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");

    // Bind the TCP socket to address
    rc = bind(*tcp_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
    DIE(rc < 0, "Unable to bind TCP socket.");

    // Listen to connections
    rc = listen(*tcp_socket, MAX_CONNS);
    DIE(rc < 0, "Unable to listen on tcp socket.");

    // Read UDP socket
    *udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    DIE(*udp_socket < 0, "Unable to read UDP socket.");

    // Turn on reuseaddr fro udp socket
    option = 1;
    if (setsockopt(*udp_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&option, sizeof(int)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");

    // Bind the UDP socket to address
    rc = bind(*udp_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
    DIE(rc < 0, "Unable to bind UDP socket.");

    // Create epoll
    *epollfd = epoll_create1(0);
    DIE(*epollfd < 0, "Unable to create epoll.");

    add_epoll_events(*epollfd, *tcp_socket, *udp_socket);
}

struct sockaddr_in set_up_server_addr(char *port)
{
    // Set up server_addr
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(atoi(port));

    return server_addr;
}
