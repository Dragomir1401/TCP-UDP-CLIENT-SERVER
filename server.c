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

// TCP clients list
linked_list_t *tcp_clients;

// Topics list
linked_list_t *topics;

char *prepare_message(char *buf, char *data, char *topic, int data_type)
{
    message message;
    memcpy(message.data, data, MAX_CONTENT_SIZE);
    memcpy(message.topic, topic, MAX_TOPIC_SIZE);
    message.data_type = data_type;

    char *sender = malloc(BUFSIZ);
    fprintf(stderr, "Sending to client %s topic %s with data_type %d with data %s.\n", buf, message.topic, message.data_type, message.data);

    memcpy(sender, message.topic, strlen(message.topic));
    strcpy(sender + strlen(message.topic), " - ");
    if (message.data_type == 0)
    {
        strcpy(sender + strlen(message.topic) + 3, "INT");
        strcpy(sender + strlen(message.topic) + 3 + 3, " - ");
        memcpy(sender + strlen(message.topic) + 3 + 3 + 3, message.data, strlen(message.data));
    }
    else if (message.data_type == 1)
    {
        strcpy(sender + strlen(message.topic) + 3, "SHORT_REAL");
        strcpy(sender + strlen(message.topic) + 3 + 10, " - ");
        memcpy(sender + strlen(message.topic) + 3 + 10 + 3, message.data, strlen(message.data));
    }
    else if (message.data_type == 2)
    {
        strcpy(sender + strlen(message.topic) + 3, "FLOAT");
        strcpy(sender + strlen(message.topic) + 3 + 5, " - ");
        memcpy(sender + strlen(message.topic) + 3 + 5 + 3, message.data, strlen(message.data));
    }
    else if (message.data_type == 3)
    {
        strcpy(sender + strlen(message.topic) + 3, "STRING");
        strcpy(sender + strlen(message.topic) + 3 + 6, " - ");
        memcpy(sender + strlen(message.topic) + 3 + 6 + 3, message.data, strlen(message.data));
    }

    sender[strlen(sender)] = '\0';

    return sender;
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

int handle_stdin(int tcp_socket, int udp_socket, linked_list_t *tcp_clients)
{
    // Stdin reply
    char *buf = malloc(MAX_SIZE);
    memset(buf, 0, MAX_SIZE);
    int rc = read(STDIN_FILENO, buf, MAX_SIZE);
    DIE(rc == -1, "Unable to read from stdin.");

    // Check to see if we need to exit the server
    if (!strncmp(buf, "exit", EXIT_SIZE))
    {
        // Send close message to all clients
        for (ll_node_t *client = tcp_clients->head; client; client = client->next)
        {
            // Send the message
            fprintf(stderr, "Sending close command to client %s.\n", ((tcp_client *)(client->data))->id);
            char *sender = prepare_message(buf, "close", "close", 3);

            // Send size + actual payload
            int size = strlen(sender) + 1;
            char *payload = malloc(MAX_SIZE);
            memcpy(payload, &size, sizeof(int));
            fprintf(stderr, "Sending size: %d\n", *((int *)payload));

            memcpy(payload + sizeof(int), sender, strlen(sender) + 1);
            fprintf(stderr, "Sending: %s\n", payload + sizeof(int));
            int rc = send(((tcp_client *)(client->data))->socket, payload, strlen(sender) + sizeof(int) + 1, 0);
            DIE(rc < 0, "Unable to send data to TCP client.");

            free(sender);
            free(payload);
        }

        // Close server connexions with TCP clients
        shutdown(tcp_socket, SHUT_RDWR);
        // Close TCP socket
        close(tcp_socket);
        // Close UDP socket
        close(udp_socket);
        // Exit loop
        exit(1);
    }
    else
    {
        printf("Invalid command.\n");
        return -1;
    }

    return 1;
}

void check_existence(linked_list_t *tcp_clients, char *buf, int *broke, int *just_status)
{
    for (ll_node_t *curr = tcp_clients->head; curr; curr = curr->next)
    {
        // If id is already written in TCP known paired clients
        if (!strncmp(((tcp_client *)(curr->data))->id, buf, strlen(buf)))
        {
            if (((tcp_client *)(curr->data))->status == 1)
            {
                *broke = 1;
                // If client is up
                printf("Client %s already connected.\n", buf);
            }
            else
            {
                fprintf(stderr, "Status set to UP for client %s.\n", buf);
                //  If client is down make it up and send data if necessary
                ((tcp_client *)(curr->data))->status = 1;
                *just_status = 1;
            }
        }
    }
}

void set_status_to_up(linked_list_t *topics, char *buf)
{
    for (ll_node_t *curr_topic = topics->head; curr_topic; curr_topic = curr_topic->next)
    {
        for (ll_node_t *sub = ((topic *)curr_topic->data)->subscribers->head; sub; sub = sub->next)
        {
            if (!strncmp(((tcp_client *)(sub->data))->id, buf, strlen(buf)))
            {
                // Modified status in topic
                fprintf(stderr, "Modified status to UP for user %s in topic %s sub list.\n", buf, ((topic *)(curr_topic->data))->topic);
                ((tcp_client *)(sub->data))->status = 1;
            }
        }
    }
}

void add_tcp_client(linked_list_t *tcp_clients, char *buf, int tcp_client_socket)
{
    // Create new TCP client if it was not found
    tcp_client *new_tcp_client = malloc(sizeof(tcp_client));
    new_tcp_client->id = malloc(strlen(buf) + 1);
    memmove(new_tcp_client->id, buf, strlen(buf));
    new_tcp_client->socket = tcp_client_socket;
    new_tcp_client->status = 1;
    new_tcp_client->topics = ll_create(MAX_TOPIC_SIZE);
    // Initialise client sf with 0
    new_tcp_client->sf = 0;

    // Add client to end of tcp clients list
    fprintf(stderr, "Adding new client %s to tcp client list.\n", buf);

    ll_add_nth_node(tcp_clients, 0, new_tcp_client);
}

void add_conn(int tcp_client_socket, struct sockaddr_in tcp_ip_addr, char *buf, int epollfd)
{
    // Add new connexion to epoll instance
    struct epoll_event event = {};
    event.data.fd = tcp_client_socket;
    event.events = EPOLLIN;
    int rc = epoll_ctl(epollfd, EPOLL_CTL_ADD, tcp_client_socket, &event);
    DIE(rc < 0, "Unable to add TCP client socket to epoll instance.");

    // Disable Nagle algorithm
    int yes = 1;
    rc = setsockopt(tcp_client_socket, IPPROTO_TCP, TCP_NODELAY, (char *)&yes, sizeof(int));
    DIE(rc < 0, "Unable to disable Nagle algorithm.");

    // Print connection message
    printf("New client %s connected from %s:%d.\n", buf, inet_ntoa(tcp_ip_addr.sin_addr), ntohs(tcp_ip_addr.sin_port));
}

void send_data_sf_on(linked_list_t *topics, char *buf)
{
    // Send all data about a topic to user if it has sf on on that topic and if the topic is not fictive
    for (ll_node_t *curr_topic = topics->head; curr_topic; curr_topic = curr_topic->next)
    {
        // If the topic is not fictive and not delivered yet
        if (((topic *)(curr_topic->data))->data_type != 5 && ((topic *)(curr_topic->data))->delivered == 0)
        {
            for (ll_node_t *sub = ((topic *)curr_topic->data)->subscribers->head; sub; sub = sub->next)
            {
                // If we found out target client
                if (!strncmp(((tcp_client *)(sub->data))->id, buf, strlen(buf)))
                {
                    // If this client had sf on
                    if (((tcp_client *)(sub->data))->sf == 1)
                    {
                        // Send data about the topic
                        char *sender = prepare_message(buf, ((topic *)curr_topic->data)->data, ((topic *)curr_topic->data)->topic, ((topic *)curr_topic->data)->data_type);

                        // Send size + actual payload
                        int size = strlen(sender) + 1;
                        char *payload = malloc(MAX_SIZE);
                        memcpy(payload, &size, sizeof(int));
                        fprintf(stderr, "Sending size: %d\n", *((int *)payload));

                        memcpy(payload + sizeof(int), sender, strlen(sender) + 1);
                        fprintf(stderr, "Sending: %s\n", payload + sizeof(int));
                        int rc = send(((tcp_client *)(sub->data))->socket, payload, strlen(sender) + sizeof(int) + 1, 0);
                        DIE(rc < 0, "Unable to send data to TCP client.");

                        free(sender);
                        free(payload);
                    }
                }
            }
        }
    }
}

int handle_tcp(int tcp_socket, linked_list_t *tcp_clients, linked_list_t *topics, int epollfd)
{
    fprintf(stderr, "Handling TCP...\n");

    // Create TCP ip address
    struct sockaddr_in tcp_ip_addr;
    socklen_t tcp_ip_addr_len = sizeof(tcp_ip_addr);

    // TCP reply
    int tcp_client_socket = accept(tcp_socket, (struct sockaddr *)&tcp_ip_addr, &tcp_ip_addr_len);
    DIE(tcp_client_socket == -1, "Accept TCP connexion error.");

    // Recieve id from TCP connexion
    char *buf = malloc(MAX_SIZE);
    memset(buf, 0, MAX_SIZE);
    int rc = recv(tcp_client_socket, buf, sizeof(buf), 0);
    DIE(rc < 0, "Unable to recieve TCP connection.");

    int already_exists = 0;
    int just_status = 0;

    // Search to see if TCP client was already connected or not
    check_existence(tcp_clients, buf, &already_exists, &just_status);

    // Set status to up in topics where he was subscribed
    set_status_to_up(topics, buf);

    if (already_exists)
    {
        // Disconnect new client
        // Send the close message
        char *sender = prepare_message(buf, "close", "close", 3);

        // Send size + actual payload
        int size = strlen(sender) + 1;
        char *payload = malloc(MAX_SIZE);
        memcpy(payload, &size, sizeof(int));
        fprintf(stderr, "Sending size: %d\n", *((int *)payload));

        memcpy(payload + sizeof(int), sender, strlen(sender) + 1);
        fprintf(stderr, "Sending: %s\n", payload + sizeof(int));
        rc = send(tcp_client_socket, payload, strlen(sender) + sizeof(int) + 1, 0);
        DIE(rc < 0, "Unable to send data to TCP client.");

        free(sender);
        free(payload);
        return -1;
    }

    if (!just_status)
    {
        add_tcp_client(tcp_clients, buf, tcp_client_socket);
    }

    // Add new connexion to epoll instance
    add_conn(tcp_client_socket, tcp_ip_addr, buf, epollfd);

    // Send all data about a topic to user if it has sf on on that topic and if the topic is not fictive
    send_data_sf_on(topics, buf);

    free(buf);
    return 1;
}

void create_new_topic(char *buf, struct sockaddr_in *client_addr, topic **new_topic)
{
    memmove((*new_topic)->topic, buf, MAX_TOPIC_SIZE);
    memmove(&((*new_topic)->data_type), buf + MAX_TOPIC_SIZE, DATA_TYPE_SIZE);

    if ((*new_topic)->data_type == 3)
    {
        // Parsing string data
        memmove((*new_topic)->data, buf + MAX_TOPIC_SIZE + DATA_TYPE_SIZE, MAX_CONTENT_SIZE);
    }
    else if ((*new_topic)->data_type == 2)
    {
        // Parsing float data
        int sign = (int)(buf[MAX_TOPIC_SIZE + DATA_TYPE_SIZE]);
        int value = ntohl(*(uint32_t *)(buf + MAX_TOPIC_SIZE + DATA_TYPE_SIZE + sizeof(char)));
        uint8_t pow = (*(uint8_t *)(buf + MAX_TOPIC_SIZE + DATA_TYPE_SIZE + sizeof(char) + sizeof(uint32_t)));

        uint8_t pow_cpy = pow;
        int divident = 1;
        while (pow_cpy != 0)
        {
            divident *= 10;
            pow_cpy--;
        }
        double payload_float_val = (sign == 1) ? (-1) * value / (divident * 1.0) : value / (divident * 1.0);
        snprintf((*new_topic)->data, MAX_CONTENT_SIZE, "%1.10g", payload_float_val);
    }
    else if ((*new_topic)->data_type == 1)
    {
        // Parsing short real data
        double payload_val = ntohs(*(uint16_t *)(buf + MAX_TOPIC_SIZE + DATA_TYPE_SIZE)) / 100.0;
        snprintf((*new_topic)->data, MAX_CONTENT_SIZE, "%.2f", payload_val);
    }
    else if ((*new_topic)->data_type == 0)
    {
        // Parsing int data
        int sign = (int)(buf[MAX_TOPIC_SIZE + DATA_TYPE_SIZE]);
        int value = ntohl(*(uint32_t *)(buf + MAX_TOPIC_SIZE + DATA_TYPE_SIZE + sizeof(char)));

        int payload_real_val = (sign == 1) ? (-1) * value : value;

        char temp[MAX_CONTENT_SIZE];
        snprintf(temp, MAX_CONTENT_SIZE, "%d", payload_real_val);
        memmove((*new_topic)->data, temp, MAX_CONTENT_SIZE);
    }

    (*new_topic)->addr = client_addr;
    (*new_topic)->subscribers = ll_create(sizeof(tcp_client));
    (*new_topic)->delivered = 0;
}

void pass_fictive_topic(linked_list_t *topics, topic *new_topic)
{
    for (ll_node_t *curr_topic = topics->head; curr_topic; curr_topic = curr_topic->next)
    {
        // If we find a fictive topic
        if (!strncmp(((topic *)curr_topic->data)->topic, new_topic->topic, strlen(new_topic->topic)) && ((topic *)curr_topic->data)->data_type == 5)
        {
            // Take its users and let it there for future users
            int usr_cnt = 0;
            for (ll_node_t *curr_user = ((topic *)curr_topic->data)->subscribers->head; curr_user; curr_user = curr_user->next)
            {
                ll_add_nth_node(new_topic->subscribers, usr_cnt, ((tcp_client *)curr_user->data));
                usr_cnt++;
                fprintf(stderr, "Added user %s from fictive to new real topic %s\n", ((tcp_client *)curr_user->data)->id, new_topic->topic);
            }
        }
    }
}

int handle_udp(int recv_fd, linked_list_t *topics, linked_list_t *tcp_clients)
{
    fprintf(stderr, "Handling UDP...\n");

    // Recieve message from UDP connexion; sender keeps the address from where we received data
    char *buf = malloc(MAX_SIZE);
    memset(buf, 0, MAX_SIZE);

    struct sockaddr_in *client_addr = malloc(sizeof(struct sockaddr));
    memset(&client_addr, 0, sizeof(client_addr));
    socklen_t client_addr_len = 0;

    int rc = recvfrom(recv_fd, buf, MAX_SIZE, MSG_WAITALL, (struct sockaddr *)&client_addr, &client_addr_len);
    DIE(rc < 0, "Unable to recieve UDP message.");

    // Create new topic based on what we recieved from UDP client
    topic *new_topic = malloc(sizeof(topic));
    create_new_topic(buf, client_addr, &new_topic);

    if (strlen(new_topic->data) > MAX_TOPIC_SIZE)
    {
        fprintf(stderr, "Recieved UDP topic %s with data_type %d with data TOO LONG TO INCLUDE.\n", new_topic->topic, new_topic->data_type);
    }
    else
    {
        fprintf(stderr, "Recieved UDP topic %s with data_type %d with data %s.\n", new_topic->topic, new_topic->data_type, new_topic->data);
    }

    int check_delivered = 0;
    // Send the message to all TCP clients that are up subscribed to this topic and are up
    for (ll_node_t *client = tcp_clients->head; client; client = client->next)
    {
        for (ll_node_t *curr_topic = (((tcp_client *)client->data)->topics)->head; curr_topic; curr_topic = curr_topic->next)
        {
            // If TCP client follows this topic
            if (!strncmp((char *)(curr_topic->data), new_topic->topic, strlen(new_topic->topic)))
            {
                // If client is up send the message
                if (((tcp_client *)client->data)->status == 1)
                {
                    // Add client to topics subscribers
                    ll_add_nth_node(new_topic->subscribers, 0, client);

                    // Send the data we recieved from UDP client
                    char *sender = prepare_message(buf, new_topic->data, new_topic->topic, new_topic->data_type);

                    // Send size + actual payload
                    int size = strlen(sender) + 1;
                    char *payload = malloc(MAX_SIZE);
                    memcpy(payload, &size, sizeof(int));
                    fprintf(stderr, "Sending size: %d\n", *((int *)payload));

                    memcpy(payload + sizeof(int), sender, strlen(sender) + 1);
                    fprintf(stderr, "Sending: %s\n", payload + sizeof(int));
                    rc = send(((tcp_client *)client->data)->socket, payload, strlen(sender) + sizeof(int) + 1, 0);
                    DIE(rc < 0, "Unable to send data to TCP client.");

                    free(sender);
                    free(payload);
                }
                else
                {
                    if (((tcp_client *)client->data)->sf == 1)
                    {
                        // If sf is on and status down it means there still are clients to receive this topic
                        check_delivered = 1;
                    }
                }
            }
        }
    }

    if (check_delivered)
    {
        new_topic->delivered = 0;
    }
    else
    {
        new_topic->delivered = 1;
    }

    // Add fictive topic subscribed users to real topic
    // Iterate to see if we find a fictive topic we have to take users from
    pass_fictive_topic(topics, new_topic);

    // Add new topic to list of topics if not delivered
    if (new_topic->delivered == 0)
    {
        ll_add_nth_node(topics, 0, new_topic);
    }

    free(buf);

    return 1;
}

tcp_client *find_client(linked_list_t *tcp_clients, int fd)
{
    tcp_client *client;
    for (ll_node_t *curr_client = tcp_clients->head; curr_client; curr_client = curr_client->next)
    {
        if (((tcp_client *)curr_client->data)->socket == fd)
        {
            client = ((tcp_client *)(curr_client->data));
        }
    }

    return client;
}

void handle_disconnect(int epollfd, char *client_id, tcp_client *client, struct epoll_event *events, int index)
{
    printf("Client %s disconnected.\n", client_id);

    // Just modify the status
    client->status = 0;

    int closed_socket = client->socket;

    // // Remove socket from epoll
    int rc = epoll_ctl(epollfd, EPOLL_CTL_DEL, closed_socket, &events[index]);
    DIE(rc < 0, "Unable to remove socket from epoll.");

    // Close the TCP client socket
    close(closed_socket);
}

void subscribe_user_to_topic(linked_list_t *topics, tcp_client *client, char *topic_targeted, int sf)
{
    for (ll_node_t *curr = topics->head; curr; curr = curr->next)
    {
        // If topic exists in list and is not a fictive topic
        if (!strncmp(topic_targeted, ((topic *)(curr->data))->topic, strlen(topic_targeted)) && ((topic *)(curr->data))->data_type != 5)
        {
            // Add client to topic subscribed clients if not present already eventually modify sf
            int exists = 0;
            for (ll_node_t *sub = ((topic *)(curr->data))->subscribers->head; sub; sub = sub->next)
            {
                if (!strncmp(((tcp_client *)sub->data)->id, client->id, strlen(client->id)))
                {
                    // It already exists in list, update sf
                    fprintf(stderr, "Client %s is already subscribed to topic %s. Updating sf...\n", client->id, topic_targeted);
                    exists = 1;
                    ((tcp_client *)sub->data)->sf = sf;
                }
            }
            if (!exists)
            {
                fprintf(stderr, "Adding client %s to list of clients for topic %s.\n", client->id, topic_targeted);
                ll_add_nth_node(((topic *)(curr->data))->subscribers, 0, client);
            }

            break;
        }
    }
}

int handle_sub(linked_list_t *topics, tcp_client *client, char *buf)
{
    buf[strlen(buf) - 1] = '\0';

    // Extract topic and sf
    char *command = malloc(MAX_COMMAND_SIZE);
    char *topic_targeted = malloc(MAX_TOPIC_SIZE);
    memset(command, 0, MAX_COMMAND_SIZE);
    memset(topic_targeted, 0, MAX_TOPIC_SIZE);

    char *p = strtok(buf, " ");
    strcpy(command, p);
    command[strlen(p)] = '\0';

    p = strtok(NULL, " ");
    strcpy(topic_targeted, p);
    topic_targeted[strlen(p)] = '\0';

    p = strtok(NULL, " ");

    // Update sf on client
    client->sf = atoi(p);

    fprintf(stderr, "Client with ID: %s subscribe to topic: %s with sf = %d.\n", client->id, topic_targeted, client->sf);

    // Subscribe user tot topic
    subscribe_user_to_topic(topics, client, topic_targeted, client->sf);

    int fictive_exists = 0;
    // Add user to fictive topic subs if exits else create a fictive topic
    for (ll_node_t *curr = topics->head; curr; curr = curr->next)
    {
        // If topic exists in list and is a fictive topic
        if (!strncmp(topic_targeted, ((topic *)(curr->data))->topic, strlen(topic_targeted)) && ((topic *)(curr->data))->data_type == 5)
        {
            fictive_exists = 1;
            // Add client to topic subscribed clients if not present already eventually modify sf
            int exists = 0;
            for (ll_node_t *sub = ((topic *)(curr->data))->subscribers->head; sub; sub = sub->next)
            {
                if (!strncmp(((tcp_client *)sub->data)->id, client->id, strlen(client->id)))
                {
                    // It already exists in list, update sf
                    fprintf(stderr, "Client %s is already subscribed to topic %s. Updating sf...\n", client->id, topic_targeted);
                    exists = 1;
                    ((tcp_client *)sub->data)->sf = client->sf;
                }
            }
            if (!exists)
            {
                fprintf(stderr, "Adding client %s to list of clients for topic %s.\n", client->id, topic_targeted);
                ll_add_nth_node(((topic *)(curr->data))->subscribers, 0, client);
            }

            break;
        }
    }

    if (!fictive_exists)
    {
        // Create fictive topic with pre subscribed users and add it to topics then delete it when we find the real one
        topic *fictive = malloc(sizeof(topic));
        fictive->data_type = 5; // Mark as fictive using 5 in data type
        memmove(fictive->topic, topic_targeted, strlen(topic_targeted));
        fictive->subscribers = ll_create(sizeof(tcp_client));
        fictive->delivered = 0;
        ll_add_nth_node(fictive->subscribers, 0, client);

        // Add fictive topic to topic list
        ll_add_nth_node(topics, 0, fictive);
    }

    // Add it to client subscribed topics if it is not already there
    fprintf(stderr, "Adding topic %s to list of topics for user %s.\n", topic_targeted, client->id);
    int broke = 0;
    for (ll_node_t *curr_topic = client->topics->head; curr_topic; curr_topic = curr_topic->next)
    {
        if (!strncmp(topic_targeted, (char *)(curr_topic->data), strlen(topic_targeted)))
        {
            // If it already is there dont add it no more
            free(command);
            free(topic_targeted);
            broke = 1;
        }
    }

    if (broke)
    {
        return -1;
    }

    // Else add it
    ll_add_nth_node(client->topics, 0, topic_targeted);

    free(command);
    free(topic_targeted);

    return 1;
}

void handle_unsub(linked_list_t *topics, tcp_client *client, char *buf)
{
    // Extract topic
    char *p = strtok(buf, " ");
    char *command = malloc(MAX_COMMAND_SIZE);
    memcpy(command, p, strlen(p));

    p = strtok(NULL, "\n");
    char *topic_targeted = malloc(MAX_TOPIC_SIZE);
    memcpy(topic_targeted, p, strlen(p));

    printf("Client with ID: %s %s to topic: %s.\n", client->id, command, topic_targeted);

    int cnt = 0;
    // Remove topic from client subscribed topics
    for (ll_node_t *curr_client_topic = client->topics->head; curr_client_topic; curr_client_topic = curr_client_topic->next)
    {

        if (!strncmp(((topic *)curr_client_topic->data)->topic, topic_targeted, strlen(topic_targeted)))
        {
            ll_remove_nth_node(client->topics, cnt);
        }
        cnt++;
    }

    // If topic exists in topic list
    for (ll_node_t *curr_topic = topics->head; curr_topic; curr_topic = curr_topic->next)
    {
        if (!strncmp(topic_targeted, ((topic *)(curr_topic->data))->topic, strlen(topic_targeted)))
        {
            cnt = 0;
            // Remove subscriber from topic subscribed clients
            for (ll_node_t *curr_subscriber = ((linked_list_t *)((topic *)(curr_topic->data))->subscribers)->head; curr_subscriber; curr_subscriber = curr_subscriber->next)
            {
                if (((tcp_client *)curr_subscriber)->id == client->id)
                {
                    ll_remove_nth_node(((topic *)(curr_topic->data))->subscribers, cnt);
                }
                cnt++;
            }
        }
    }

    free(buf);
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
    int rc = setsockopt(*tcp_socket, IPPROTO_TCP, TCP_NODELAY, &(int){1}, sizeof(int));
    DIE(rc < 0, "Unable to disable Nagle algorithm.");

    // Turn on reuseaddr for tcp socket
    if (setsockopt(*tcp_socket, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0)
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
    if (setsockopt(*udp_socket, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");

    // Bind the UDP socket to address
    rc = bind(*udp_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
    DIE(rc < 0, "Unable to bind UDP socket.");

    // Create epoll
    *epollfd = epoll_create1(0);
    DIE(*epollfd < 0, "Unable to create epoll.");

    add_epoll_events(*epollfd, *tcp_socket, *udp_socket);
}

int main(int argc, char *argv[])
{
    // Declare sockets
    int tcp_socket, udp_socket, epollfd, eventfd;

    // Create list of TCP clients connected
    linked_list_t *tcp_clients = ll_create(sizeof(tcp_client));

    // Create topic list
    linked_list_t *topics = ll_create(sizeof(topic));

    // Set up server_addr
    struct sockaddr_in server_addr = set_up_server_addr(argv[1]);

    // Prepare connections
    prepare_conn(&udp_socket, &tcp_socket, server_addr, argc, argv, &epollfd, &eventfd);

    while (1)
    {
        struct epoll_event events[MAX_CONNS];
        int num_events = epoll_wait(epollfd, events, MAX_CONNS, TIMEOUT);
        DIE(num_events < 0, "Epoll wait error.");

        for (int i = 0; i < num_events; i++)
        {
            if (events[i].data.fd == STDIN_FILENO)
            {
                if (handle_stdin(tcp_socket, udp_socket, tcp_clients) == -1)
                    continue;
            }
            else if (events[i].data.fd == tcp_socket)
            {
                if (handle_tcp(tcp_socket, tcp_clients, topics, epollfd) == -1)
                    continue;
            }
            else if (events[i].data.fd == udp_socket)
            {
                handle_udp(udp_socket, topics, tcp_clients);
            }
            else
            {
                // Recieve message from TCP connexion
                char *buf = malloc(MAX_SIZE);
                memset(buf, 0, MAX_SIZE);
                int rc = recv(events[i].data.fd, buf, MAX_SIZE, 0);
                DIE(rc < 0, "Unable to receive TCP message.");

                // Find client
                tcp_client *client = find_client(tcp_clients, events[i].data.fd);

                // Case where TCP client disconnected
                if (!strncmp(buf, "exit", CLOSE_MESSAGE_LEN - 1))
                {
                    handle_disconnect(epollfd, client->id, client, events, i);
                    continue;
                }
                // Subscribe command
                else if (!strncmp(buf, "subscribe", SUBSCRIBE_COMMAND_LEN))
                {
                    if (handle_sub(topics, client, buf) == -1)
                        continue;
                }
                // Unsubscribe command
                else if (!strncmp(buf, "unsubscribe", UNSUBSCRIBE_COMMAND_LEN))
                {
                    handle_unsub(topics, client, buf);
                    continue;
                }
            }
        }
    }

    return 0;
}
