#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <algorithm>
#include <iterator>
#include "protocols.h"
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

bool client_is_valid(tcp_client clients[], char client_id[], int &number_of_clients)
{
    for (int i = 0; i < number_of_clients; i++)
    {
        if (!strcmp(clients[i].id, client_id) && clients[i].active)
        {
            return false;
        }
    }

    return true;
}

void disconnect_client(int sockfd)
{
    string acc = "close";

    // Send size + actual payload
    char *buffer = (char *)malloc(MAX_SIZE);

    // FIrst 4 bytes of size
    int size = acc.size() + 1;
    memcpy(buffer, &size, sizeof(int));

    // Next byes of payload
    acc.copy(buffer + sizeof(int), size);
    buffer[size + sizeof(int) - 1] = '\0';

    // Send message
    int rc = send(sockfd, buffer, size + sizeof(int), 0);
    DIE(rc < 0, "Unable to send data to TCP client.");

    free(buffer);
}

bool hasKeyV1(const unordered_map<string, queue<message>> &map, const string &key)
{
    for (auto it = map.cbegin(); it != map.cend(); ++it)
    {
        if (it->first == key)
        {
            return true;
        }
    }
    return false;
}

bool hasKeyV2(unordered_map<string, int> map, string key)
{
    return map.count(key) ? true : false;
}

void send_message(message msg, int sockfd)
{
    string acc = msg.topic + " - " + msg.data_type + " - " + msg.payload;

    // Send size + actual payload
    char *buffer = (char *)malloc(MAX_SIZE);

    // FIrst 4 bytes of size
    int size = acc.size() + 1;
    memcpy(buffer, &size, sizeof(int));

    // Next byes of payload
    acc.copy(buffer + sizeof(int), size);
    buffer[size + sizeof(int) - 1] = '\0';

    // Send message
    int rc = send(sockfd, buffer, size + sizeof(int), 0);
    DIE(rc < 0, "Unable to send data to TCP client.");

    free(buffer);
}

void activate_client(unordered_map<string, queue<message>> &inactive_list, tcp_client &client)
{
    client.active = true;

    // If client has inactive messages, send them
    if (hasKeyV1(inactive_list, client.id))
    {
        // If there are messages to send in the inactive list
        if (inactive_list.count(client.id) > 0)
        {
            // While there are still messages left
            while (!inactive_list.at(client.id).empty())
            {
                // Send the received messages while client was inactive
                message msg = inactive_list.at(client.id).front();
                inactive_list.at(client.id).pop();
                send_message(msg, client.socket);
            }
        }
    }
}

bool handle_reconnecting_client(tcp_client clients[], char client_id[], int &number_of_clients,
                                struct sockaddr_in tcp_addr, unordered_map<string, queue<message>> &inactive_list)
{
    for (int i = 0; i < number_of_clients; i++)
    {
        // Client is reconnecting because it already is in database
        if (!strcmp(clients[i].id, client_id))
        {
            // Set client to active
            printf("New client %s connected from %s : %d\n",
                   client_id, inet_ntoa(tcp_addr.sin_addr), ntohs(tcp_addr.sin_port));

            activate_client(inactive_list, clients[i]);

            return true;
            break;
        }
    }

    return false;
}

int handle_tcp(int tcp_socket, int epollfd, char buffer[MAX_SIZE], tcp_client clients[],
               int &number_of_clients, struct epoll_event *events, int index,
               unordered_map<string, queue<message>> &inactive_list)
{
    // Create TCP ip address
    struct sockaddr_in tcp_ip_addr;
    memset(&tcp_ip_addr, 0, sizeof(tcp_ip_addr));
    socklen_t tcp_ip_addr_len = sizeof(tcp_ip_addr);

    // TCP reply
    int new_socket = accept(tcp_socket, (struct sockaddr *)&tcp_ip_addr, &tcp_ip_addr_len);
    DIE(new_socket == -1, "Accept TCP connexion error.");

    // Add new connexion to epoll instance
    struct epoll_event event = {};
    event.data.fd = new_socket;
    event.events = EPOLLIN;
    int rc = epoll_ctl(epollfd, EPOLL_CTL_ADD, new_socket, &event);
    DIE(rc < 0, "Unable to add TCP client socket to epoll instance.");

    // Receive message
    memset(buffer, 0, MAX_SIZE);
    rc = recv(new_socket, buffer, MAX_SIZE, 0);
    DIE(rc < 0, "Cannot receive message from TCP client.");

    if (!client_is_valid(clients, buffer, number_of_clients))
    {
        // Client is already connected
        printf("Client %s already connected.\n", buffer);
        disconnect_client(new_socket);

        // Remove socket from epoll
        int rc = epoll_ctl(epollfd, EPOLL_CTL_DEL, new_socket, &events[index]);
        DIE(rc < 0, "Unable to remove socket from epoll.");

        close(new_socket);

        return -1;
    }
    else
    {
        // Handle reconnecting client
        if (!handle_reconnecting_client(clients, buffer, number_of_clients, tcp_ip_addr, inactive_list))
        {
            // We have to create a actual new client and add it
            tcp_client new_client;
            strncpy(new_client.id, buffer, strlen(buffer) + 1);
            new_client.socket = new_socket;
            new_client.active = true;
            new_client.topics_count = 0;
            clients[number_of_clients++] = new_client;

            printf("New client %s connected from %s : %d\n",
                   buffer, inet_ntoa(tcp_ip_addr.sin_addr), ntohs(tcp_ip_addr.sin_port));

            // Disable Nagle algorithm on the new client
            int option = 1;
            rc = setsockopt(new_socket, IPPROTO_TCP, TCP_NODELAY, (char *)&option, sizeof(int));
            DIE(rc < 0, "Unable to disable Nagle algorithm.");
        }
    }

    // Success
    return 1;
}

void separate_udp_message(char buffer[MAX_SIZE], message *msg, int size)
{
    msg->topic = buffer;

    int type = (int)buffer[MAX_TOPIC_SIZE];
    int val, sign, exp;
    uint8_t pow, pow1;
    double fval;
    char aux[MAX_CONTENT_SIZE];

    switch (type)
    {
    case 0:
        msg->data_type = "INT";
        sign = (int)buffer[MAX_TOPIC_SIZE + 1];
        val = ntohl(*(uint32_t *)(buffer + MAX_TOPIC_SIZE + 2));
        val = sign == 1 ? -val : val;
        snprintf(aux, MAX_CONTENT_SIZE, "%d", val);
        msg->payload = aux;
        break;

    case 1:
        msg->data_type = "SHORT_REAL";
        fval = ntohs(*(uint16_t *)(buffer + MAX_TOPIC_SIZE + 1)) / 100.0;
        snprintf(aux, MAX_CONTENT_SIZE, "%.2f", fval);
        msg->payload = aux;
        break;

    case 2:
        msg->data_type = "FLOAT";
        sign = (int)buffer[MAX_TOPIC_SIZE + 1];
        val = ntohl(*(uint32_t *)(buffer + MAX_TOPIC_SIZE + 2));
        pow = (*(uint8_t *)(buffer + MAX_TOPIC_SIZE + 6));
        pow1 = pow;
        exp = 1;
        while (pow1 != 0)
        {
            exp *= 10;
            pow1--;
        }
        fval = val / (exp * 1.0);
        fval = sign == 1 ? -fval : fval;
        snprintf(aux, MAX_CONTENT_SIZE, "%1.10g", fval);
        msg->payload = aux;
        break;

    case 3:
        msg->data_type = "STRING";
        msg->payload = buffer + MAX_TOPIC_SIZE + 1;
        break;

    default:
        fprintf(stderr, "Invalid message type.\n");
        break;
    }
}
void create_new_message(message &msg, struct sockaddr_in udp_ip_addr)
{
    // Create message
    memset(&msg, 0, sizeof(msg));
    msg.ip_udp = inet_ntoa(udp_ip_addr.sin_addr);
    char aux[6];
    sprintf(aux, "%d", ntohs(udp_ip_addr.sin_port));
    msg.port_udp = aux;
}

void send_to_clients(tcp_client clients[], int &number_of_clients, message msg,
                     unordered_map<string, queue<message>> &inactive_list)
{
    // Send message to all clients
    for (int i = 0; i < number_of_clients; i++)
    {
        // If client is active and subscribed to topic
        if (hasKeyV2(clients[i].topics, msg.topic))
        {
            fprintf(stderr, "Sending message to client %s.\n", clients[i].id);
            if (clients[i].active)
            {
                send_message(msg, clients[i].socket);
            }
        }

        // If client is not active and subscribed to topic
        if (!clients[i].active && hasKeyV2(clients[i].topics, msg.topic))
        {
            // Save message for later for clients subscribed with sf 1
            if (hasKeyV1(inactive_list, clients[i].id))
            {
                if (clients[i].topics.at(msg.topic) == 1)
                {
                    inactive_list[clients[i].id].push(msg);
                }
            }

            if (clients[i].topics.at(msg.topic) == 1)
            {
                queue<message> q;
                q.push(msg);
                inactive_list.insert(pair<string, queue<message>>(clients[i].id, q));
            }
        }
    }
}

int handle_udp(int udp_socket, int epollfd, char buffer[MAX_SIZE], tcp_client clients[],
               int &number_of_clients, struct epoll_event *events, int index,
               unordered_map<string, queue<message>> &inactive_list)
{
    memset(buffer, 0, MAX_SIZE);
    struct sockaddr_in udp_ip_addr;
    memset(&udp_ip_addr, 0, sizeof(udp_ip_addr));
    socklen_t udp_ip_addr_len = sizeof(udp_ip_addr);

    int rc = recvfrom(udp_socket, (char *)buffer, MAX_SIZE,
                      MSG_WAITALL, (struct sockaddr *)&udp_ip_addr,
                      &udp_ip_addr_len);
    DIE(rc < 0, "Cannot receive message from UDP client.");

    // Create message
    message msg;
    create_new_message(msg, udp_ip_addr);

    // Parse input
    separate_udp_message(buffer, &msg, rc);

    // Send to clients
    send_to_clients(clients, number_of_clients, msg, inactive_list);

    // Success
    return 1;
}

int handle_stdin(char buffer[MAX_SIZE], int number_of_clients, tcp_client clients[], int epollfd,
                 struct epoll_event *events, int index)
{
    memset(buffer, 0, MAX_SIZE);
    int rc = read(STDIN_FILENO, buffer, MAX_SIZE);
    DIE(rc < 0, "Cannot read from stdin.");
    buffer[rc - 1] = '\0';

    if (strcmp(buffer, "exit") == 0)
    {
        fprintf(stderr, "Sending close message to all clients.\n");
        for (int i = 0; i < number_of_clients; i++)
        {
            if (clients[i].active)
            {
                disconnect_client(clients[i].socket);

                // Remove socket from epoll
                int rc = epoll_ctl(epollfd, EPOLL_CTL_DEL, clients[i].socket, &events[index]);
                DIE(rc < 0, "Unable to remove socket from epoll.");

                close(clients[i].socket);
            }
        }

        return 0;
    }
    else
    {
        fprintf(stderr, "Invalid command.\n");
        return -1;
    }

    return 0;
}

void handle_commands(char buffer[MAX_SIZE], int number_of_clients, tcp_client clients[], int sockfd)
{
    char topic_targeted[MAX_TOPIC_SIZE];
    int sf;
    memset(topic_targeted, 0, MAX_TOPIC_SIZE);

    if (buffer[strlen(buffer) - 3] == 'S')
    {
        strncpy(topic_targeted, buffer, strlen(buffer) - 3);
        if (buffer[strlen(buffer) - 2] == '1')
        {
            sf = 1;
        }
        else
        {
            sf = 0;
        }

        fprintf(stderr, "Received subscribe from client to %s with sf %d.\n", topic_targeted, sf);

        for (tcp_client *ptr = clients, *end = clients + number_of_clients; ptr != end; ++ptr)
        {
            bool present = hasKeyV2(ptr->topics, topic_targeted);
            if (ptr->socket == sockfd && present)
            {

                ptr->topics[topic_targeted] = sf;
            }
            else if (ptr->socket == sockfd)
            {
                ptr->topics.insert(std::make_pair(topic_targeted, sf));
            }
        }
    }
    else if (buffer[strlen(buffer) - 1] == 'U')
    {
        strncpy(topic_targeted, buffer, strlen(buffer) - 1);
        fprintf(stderr, "Received unsubscribe to topic %s.\n", topic_targeted);

        for (int i = 0; i < number_of_clients; i++)
        {
            if (clients[i].socket == sockfd)
            {
                clients[i].topics.erase(topic_targeted);
            }
        }
    }

    // TO DO : EXIT
}

void deactivate_client(tcp_client clients[], int number_of_clients, int sockfd)
{
    auto client = std::find_if(&clients[0], &clients[number_of_clients], [&](const auto &c)
                               { return c.socket == sockfd; });

    if (client != &clients[number_of_clients])
    {
        client->active = false;
        printf("Client %s disconnected.\n", client->id);
    }
}

int receive_from_tcp(char buffer[MAX_SIZE], int sockfd, int number_of_clients, tcp_client clients[],
                     int epollfd, struct epoll_event *events, int index)
{
    memset(buffer, 0, MAX_SIZE);

    int rc = recv(sockfd, buffer, MAX_SIZE, 0);
    DIE(rc < 0, "Cannot receive message from TCP client.");

    if (!rc)
    {
        // Set client status to inactive
        deactivate_client(clients, number_of_clients, sockfd);

        // Remove socket from epoll
        int rc = epoll_ctl(epollfd, EPOLL_CTL_DEL, sockfd, &events[index]);
        DIE(rc < 0, "Unable to remove socket from epoll.");

        // Close connection
        close(sockfd);

        return -1;
    }

    // Handle commands from client
    handle_commands(buffer, number_of_clients, clients, sockfd);

    return 1;
}

int main(int argc, char *argv[])
{
    // Declare sockets
    int tcp_socket, udp_socket, epollfd, eventfd;

    // Set up server_addr
    struct sockaddr_in server_addr = set_up_server_addr(argv[1]);

    // Prepare connections
    prepare_conn(&udp_socket, &tcp_socket, server_addr, argc, argv, &epollfd, &eventfd);

    // Create inactive list of clietns for sf
    unordered_map<string, queue<message>> inactive_list;

    // Create active list of clients
    tcp_client clients[MAX_CONNS];
    int number_of_clients = 0;

    // Create buffer
    char buffer[MAX_SIZE];

    while (1)
    {
        struct epoll_event events[MAX_CONNS];
        int num_events = epoll_wait(epollfd, events, MAX_CONNS, TIMEOUT);
        DIE(num_events < 0, "Epoll wait error.");

        for (int i = 0; i < num_events; i++)
        {
            if (events[i].data.fd == tcp_socket)
            {
                if (handle_tcp(tcp_socket, epollfd, buffer, clients,
                               number_of_clients, events, i, inactive_list) == -1)
                {
                    continue;
                }
            }
            else if (events[i].data.fd == udp_socket)
            {
                if (handle_udp(udp_socket, epollfd, buffer, clients,
                               number_of_clients, events, i, inactive_list) == -1)
                {
                    continue;
                }
            }
            else if (events[i].data.fd == STDIN_FILENO)
            {
                if (handle_stdin(buffer, number_of_clients, clients, epollfd, events, i) == -1)
                {
                    continue;
                }
                else
                {
                    close(tcp_socket);
                    close(udp_socket);
                    close(epollfd);
                    close(eventfd);
                    return 0;
                }
            }
            else if (events[i].events & EPOLLIN)
            {
                // Received message from TCP clients
                if (receive_from_tcp(buffer, events[i].data.fd, number_of_clients, clients,
                                     epollfd, events, i) == -1)
                {
                    continue;
                }
            }
        }
    }
    return 0;
}
