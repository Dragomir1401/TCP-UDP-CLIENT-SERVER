______________________________
|                            |
|   Copyright                |
|      Dragomir Andrei 2023  |
|                            |
|____________________________|

---------------------------------------------------------------------
Nume: Dragomir Andrei
Grupa: 322CA

NETWORK PROTOCOLS

----------------------------------------------------------------------------
------------------------CLIENT SERVER MESSAGE BROKER------------------------
----------------------------------------------------------------------------

			CONTENT

	- server.cpp - implementation of the server 

        - prepare_server.cpp - functions used by the server to start and manage 
                                the connection, as well as create epoll 

        - subscriber.cpp - implementation of the client 

	- Makefile.

	- readme.txt.


----------------------------------------------------------------------------

____________Description___________


        The project implements a client server application which functions
like a notification center. The server receives data from a UDP client in 
a flow and stores it to eventually send it to TCP clients(subscribers) that
are subscribed to certain subjects they choose.

        The server is capable of receiving, storing and forwarding data to other
clients. It can also sustain subscribers that want to be up to date whith every
topic they subscribe to even if they are not connected when data is received in 
server. This is called a store and forward functionality.

        The user can send commands like subscribe/unsubscribe to the server to
signal the fact that he wants to receive data related to a subject or does not
want to receive it anymore.


        The server is capable of receving and storing 4 types of data:

        - Integer without sign:
Signed octet followed by an uint32_t formatted according to network byte order.

        - Positive real number with 2 decimal places:
An uint16_t representing the absolute value of the number multiplied by 100.

        - Floating point number:
A signed octet followed by an uint32_t (in network byte order) representing the
absolute value of the number, obtained by concatenating the integer part and 
the decimal part of the number, followed by an uint8_t representing the absolute
value of the negative power of 10 to multiply the absolute value of the number
to get the original number (in absolute value).

        - String:
A string of maximum 1500 characters, terminated by \0 or delimited by the end
of the datagram for shorter lengths.

*The sign octet will be 0 for positive numbers, 1 for negative numbers.



-----------------------------------------------------------------------------



------------------------------------------------------------------------------

____________Implementation____________


    The entire assignment was implemented in the program with all the
requirements.

        --------SERVER--------

        The server uses epoll for multiplexing connections. It uses the 
        prepare_conn function to:

        - Turn off buffering at stdout
        - Check validity of input
        - Turn off Nagle algorithm
        - Turn on reuseaddr for tcp socket
        - Bind the TCP socket 
        - Bind the UDP socket
        - Listen to connections
        - Create epoll

        MESSAGE PROTOCOL

        The server send messages in format:
        
        /------------------------------------------------------------\
        |  4 bytes   | up to 50 bytes |  1 byte   | up to 1500 bytes |
        |------------------------------------------------------------|
        | total_size |    topic       | data_type |     payload      |
        \------------------------------------------------------------/

	Then the server follows events signaled by epoll to see which functionality to
        use:

        - handle_tcp ----> It received a new connection and has to accept it.
                           The client sent it unique id to the server to
                           be created in the database.

                [/!\] Step_1 - accept connection.

                [/!\] Step_2 - add connection to epoll.

                [/!\] Step_3 - receive client id and check if it is already connected or not.

                [/!\] Step_4 - check to see if client is already in database and is only
                               reconnecting.

                [/!\] Step_5 - if it is reconnecting then set its status to active and then
                               send all the messages he missed if he has store and forward 
                               option active.



        - handle_udp ----> It received data in server.

                [/!\] Step_1 - receive message from an UDP client meaning it has to be stored
                               and sent to TCP clients. Receiving is done with an recvfrom_wrapper.

                [/!\] Step_2 - create a new message by parsing the input received from the UDP client.
                               Send the message created only to the clients who are subscribed to that
                               topic.

                [/!\] Step_3 - if client is subscribed to the topic but is not active we add that
                               topic to a queue of messages stored as value for the key = topic
                               in a map.


        - handle_stdin ----> A command from stdin was received.
                
                [/!\] Step_1 - read from stdin socket.

                [/!\] Step_2 - check to see if command is exit then remove each client
                               connection from epoll and disconnect them. After This
                               close all the sockets used.



        - none of anterior cases ----> A message from the tcp client was received.
                        
                [/!\] Step_1 - receive from the client socket.

                [/!\] Step_2 - if we did not receive anything it means the client
                               closed.

                [/!\] Step_3 - if we received subscribe we add the topic that the user
                               wants to be subscribed to to their subscribtions list. if
                               the topic is already there we just update the store and
                               forward option.

                [/!\] Step_4 - if we received unsubscribe we remove the topic that the user
                               wants from their subscribtions list.
                        
      


        --------SUBSCRIBER--------

        The subscriber uses epoll for multiplexing connections. It uses the 
        prepare_conn function to:

        - Check correct usage
        - Turn off buffering at stdout
        - Turn off Nagle algorithm
        - Add the TCP socket to epoll
        - Add the stdin socket to epoll

        Then the server follows events signaled by epoll to see which functionality to
        use:

        - stdin ----> A command from stdin was received.
                
                [/!\] Step_1 - read from stdin socket.

                [/!\] Step_2 - check to see if command is exit then close the socket if it is.

                [/!\] Step_3 - check to see if command is subscribe then create a buffer with:

                        <topic>S<0/1>
                                and send it to the server.

                [/!\] Step_4 - check to see if command is unsubscribe then create a buffer with:

                        <topic>U
                                and send it to the server.

        - else ----> A package from the server was received.
                
                [/!\] Step_1 - use recv_wrapper which receives the size of the package
                               then uses a loop to receive exactly size bytes.

                [/!\] Step_2 - check to see if command is close then close the socket if it is.    

                [/!\] Step_3 - if command is not exit, it means is data and we have to print it
                               in the subscriber.          



------------------------------------------------------------------------------




------------------------------------------------------------------------------

_____________Comments_____________

	
	The implementation could have been done faster and better if I 
understood the theoretic part easier and realized how to debug network
problems on sockets.

        The more complicated part was making sure I dont put an address
wrong at any level or that I do check for all the corruption cases and also
to use correctly the data structures.

        After completing the implementation I can say I have a better
understanding of how a TCP and UDP protocols work and how to use the
socket API as well as the epoll one.


------------------------------------------------------------------------------



------------------------------------------------------------------------------


______________How to run it_____________


	make
- creates the server and subscriber executable

        sudo python3 test.py
- runs the automated test

        ./server <port>
        ./subscriber <id> <server_ip> <server_port>


-----------------------------------------------------------------------------




-----------------------------------------------------------------------------


Resources:

https://gitlab.cs.pub.ro/pcom/homework2-public
https://pcom.pages.upb.ro/labs/
https://eclass.teicrete.gr/modules/document/file.php/TP326/%CE%98%CE%B5%CF%89%CF%81%CE%AF%CE%B1%20(Lectures)/Computer_Networking_A_Top-Down_Approach.pdf

-----------------------------------------------------------------------------
