/*
*
*  Shantanu Patil (patil2)
*  CSCI 4210 (Section 01)
*  Homework 4
*
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Defines
#define MAX_CLIENTS 32
#define MAX_CONNECTIONS 64
#define MAXMSG 1024

// Typedefs
struct ChatClient		// There is a linked list of these structures, one per connected client
{
	char *name;			// clients name string
	int fd;			// fd that the client is listening on
	int connType;			// connection type 1=TCP 2=UDP
	struct sockaddr_in sock;	// socket details
	pthread_t tid;		// thread id of this client
	struct ChatClient *next;
};

typedef struct	// passed as argument to clientFunction via pthread_create
{
	int fd;
	struct sockaddr_in sock;
} ThreadArg;


// Global variables
struct ChatClient* connectedClients = NULL;
struct ChatClient* lastClient;
int numClientsConnected = 0;

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_t threadId [MAX_CLIENTS];

// Function prototypes
void* TCPclientFunction(void*);
void UDPclientFunction(ThreadArg*, char*);
void UDPoutput(ThreadArg*, char*);
void nodeListAdd (struct ChatClient*);

void nodeListAdd (struct ChatClient* newNode)
{
	struct ChatClient *p, *prevNode;

	// add the new node is SORTED ORDER of client names
	pthread_mutex_lock(&lock);
	if (connectedClients == NULL)
	{
		// if the list is empty this is the first client being added
		connectedClients = lastClient = newNode;
	}
	else
	{
		for (p = prevNode = connectedClients; p != NULL; prevNode = p, p = p->next)
		{
			if (strcmp(newNode->name, p->name) < 0)
			{
				// we're just past the name we should be adding at
				// new node needs to be added before p
				newNode->next = p;

				if (p == connectedClients)	// new node is now @ the head of the list
				connectedClients = newNode;
				else
				{
					prevNode->next = newNode;
				}
				break;
			}
		}

		if (p == NULL)	// we're past the list, so newNode will be added to the end
		{
			// add new client to the end of the list
			//printf (" adding to end\n");
			prevNode->next = newNode;
			lastClient = newNode;
		}
	}
	numClientsConnected++;
	pthread_mutex_unlock(&lock);
	//printf ("NAME_LIST: head=%s last=%s: ", connectedClients->name, lastClient->name);
	//for (p = connectedClients; p != NULL; p = p->next)
	//  printf ("%s ", p->name);
}

void* TCPclientFunction(void* arg)
{
	ThreadArg* t;
	char buffer[MAXMSG];
	char command[16];
	char argString[MAXMSG];	// holds the command arguments like the message contents
	char msgLenString[4];		// holds the character string for message length
	int msgLen;			// length of the message
	int nbytes;
	fd_set readfds;
	int i;
	struct ChatClient* me;

	if ((t = (ThreadArg *)arg) == NULL)
	{
		return (NULL);
	}
	if ((me = (struct ChatClient *) malloc(sizeof(struct ChatClient))) == NULL)
	{
		return (NULL);
	}

	me->fd = t->fd;
	me->sock = t->sock;
	me->connType = 1;	// this is a TCP connection
	me->tid = pthread_self();
	me->next = (struct ChatClient *)NULL;


	// dprintf (t->fd, "CHILD %p will read from fd=%d\n", (void *)pthread_self(), t->fd);
	while (1)
	{
		// Initialize the set of active sockets xs
		FD_ZERO(&readfds);
		FD_SET(me->fd, &readfds);
		bzero(buffer, sizeof(buffer));
		bzero(command, sizeof(command));
		bzero(argString, sizeof(argString));

		//dprintf(me->fd, "calling select with readfds=%p\n", (void *)(readfds.__fds_bits));
		// Block until input arrives on one or more active sockets.
		if (select (FD_SETSIZE, &readfds, NULL, NULL, NULL) < 0)
		{
			//dprintf (me->fd, "CHILD %u: select() failed with errno=%d\n", (unsigned int)pthread_self(), errno);
			free(me);
			pthread_exit(NULL);
		}

		// dprintf (me->fd, "CHILD %p: select() returned. readfds=%p\n",
		//       (void *)pthread_self(), (void *)(readfds.__fds_bits));
		/* Service all the sockets with input pending. */
		for (i = 0; i < FD_SETSIZE; i++)
		{
			if (FD_ISSET(i, &readfds))
			{
				if (i == me->fd)
				{
					nbytes = read(me->fd, buffer, MAXMSG);
					if (nbytes < 0)
					{
						// some read error
						perror ("ClientFunction: read() failed");
						exit(EXIT_FAILURE);
					}
					else if (nbytes == 0)
					{
						//Client disconnected
						printf ("CHILD %u: Client disconnected\n", (unsigned int)pthread_self());

						#if 0
						struct ChatClient *p, *prevNode;
						//Remove client from list of connected clients
						pthread_mutex_lock(&lock);
						for (p = prevNode = connectedClients; p != NULL; prevNode = p, p = p->next)
						{
							if (p->fd == me->fd) // found the node to be deleted
							{
								if (p == connectedClients)	// first node in the list to be removed?
								{
									connectedClients = p->next;
								}
								else
								{
									prevNode->next = p->next;
									if (p == lastClient)	// last node in the list to be deleted?
									{
										lastClient = prevNode;
									}
								}
								numClientsConnected--;
							}
						}
						pthread_mutex_unlock(&lock);
						close(me->fd);
						free(me);
						#endif

						return (NULL);
					}
					sscanf (buffer, "%s %s", command, argString);
					//dprintf (me->fd, "CHILD: command=%s argString=%s\n", command, argString);
					//printf ("CHILD %p: command %s with argument %s\n", (void *)pc->tid, command, argString);

				}
			}
		}

		if (strcmp(command, "LOGIN") == 0)	// LOGIN command received
		{
			struct ChatClient *p;
			int go = 1;
			// dprintf(me->fd, "processing LOGIN for %s\n", argString);
			// check to see if this client has already logged in
			printf ("CHILD %u: Rcvd LOGIN request for userid %s\n", (unsigned int)me->tid, argString);
			if(strlen(argString) < 3 || strlen(argString) > 16)
			{
				dprintf(me->fd, "ERROR Invalid userid\n");
				printf("CHILD %u: Sent ERROR (Invalid userid)\n", (unsigned int)pthread_self());
				go = 0;
			}
			if ((me->name = (char *)malloc(strlen(argString+1))) == NULL)
			{
				perror ("clientFunction malloc failed ");
				exit(EXIT_FAILURE);
			}
			strcpy (me->name, argString);
			for (p = connectedClients; p != NULL; p = p->next)
			{
				if (strcmp(p->name, argString) == 0)
				{
					dprintf(me->fd, "ERROR Already connected\n");	// client is already connected
					printf("CHILD %u: Sent ERROR (Already connected)\n", (unsigned int)pthread_self());
					go = 0;
					//continue; // continue the while loop
				}
			}
			// this is a valid new client logging in
			dprintf(me->fd, "OK!\n");
			if(go == 1)
			{
				// Add the client to the linked list of clients
				nodeListAdd(me);
			}
		}
		else if (strcmp(command, "LOGOUT") == 0)
		{
			struct ChatClient *p, *prevNode;

			//dprintf(me->fd, "processing LOGOUT comnmand for fd=%d\n", me->fd);
			printf("CHILD %u: Rcvd LOGOUT request\n", (unsigned int)pthread_self());
			dprintf(me->fd, "OK!\n");

			pthread_mutex_lock(&lock);
			for (p = prevNode = connectedClients; p != NULL; prevNode = p, p = p->next)
			{
				if (p->fd == me->fd) // found the node to be deleted
				{
					//dprintf(me->fd, "LOGOUT: found fd=%d\n", p->fd);
					if (p == connectedClients)	// first node in the list to be removed?
					{
						connectedClients = p->next;
					}
					else
					{
						prevNode->next = p->next;
						if (p == lastClient)	// last node in the list to be deleted?
						{
							lastClient = prevNode;
						}
					}
					numClientsConnected--;
					pthread_mutex_unlock(&lock);
					char test[1024];
					int inBytes = read(me->fd, test, 1024); //test for client disconnect
					if(inBytes == 0)
					{
						printf ("CHILD %u: Client disconnected\n", (unsigned int)pthread_self());
						close (me->fd);
						free(me);
						return (NULL);
					}
				}
			}
			pthread_mutex_unlock(&lock);
			// remove the Client node from the linked list
			//printf ("CHILD %p: Rcvd LOGOUT request for userid %s\n", (void *)p->tid, p->name);
		}
		else if (strcmp(command, "WHO") == 0)
		{
			struct ChatClient *p;

			//dprintf (me->fd, "processing WHO command\n");
			printf("CHILD %u: Rcvd WHO request\n", (unsigned int)pthread_self());
			dprintf (me->fd, "OK!\n");
			for (p = connectedClients; p != NULL; p = p->next)
			{
				dprintf(me->fd, "%s\n", p->name);
			}
		}
		else if (strcmp(command, "BROADCAST") == 0)
		{
			struct ChatClient *p;
			int n; // index into buffer string where the message content begins

			//dprintf (me->fd, "processing BROADCAST command\n");
			printf("CHILD %u: Rcvd BROADCAST request\n", (unsigned int)pthread_self());
			dprintf(me->fd, "OK!\n");
			sscanf(buffer, "%s %s\n%s", command, msgLenString, argString);
			msgLen = atoi(msgLenString);

			n = 10;		// offset at which msgLenString begins in buffer
			if (msgLen < 10)
			{
				n += 2;
			}
			else if (msgLen < 100)
			{
				n += 3;
			}
			else		// max message will be a 3-digit number
			{
				n += 4;
			}

			// Now n is the offset at which the message content begins
			char outp[1024];
			sprintf(outp, "FROM %s %d %s", me->name, msgLen, &buffer[n]);
			for (p = connectedClients; p != NULL; p = p->next)
			{
				write(p->fd, outp, strlen(outp));
			}
		}
		else if (strcmp(command, "SEND") == 0)
		{
			struct ChatClient *p;
			// struct ChatClient *reciever;	// points to the recipient of the message
			char userId[17];
			char* pMesg;
			int destFd = -1;

			sscanf (buffer, "%s %s %s\n%s", command, userId, msgLenString, argString);
			msgLen = atoi(msgLenString);
			// dprintf (me->fd, "processing SEND command to %s: %d bytes starting with %s\n",
			// userId, msgLen, argString);

			printf("CHILD %u: Rcvd SEND request to userid %s\n", (unsigned int)pthread_self(), userId);
			if ((msgLen > 990) || (msgLen < 1))
			{
				dprintf (me->fd, "ERROR Invalid msglen\n");
				printf("CHILD %u: Sent ERROR (Invalid msgLen)\n", (unsigned int)pthread_self());
				continue;
			}

			// traverse the list of clients to find the named recipient
			for (p = connectedClients; p != NULL; p = p->next)
			{
				if (strcmp(userId, p->name) == 0)
				{
					// found the recipient we're looking for
					destFd = p->fd;
					pMesg = strstr(buffer, argString);
					break;
				}
			}

			if (destFd == -1)
			{
				dprintf (me->fd, "ERROR Unknown userid\n");	// recipient not in the clients list
				printf("CHILD %u: Sent ERROR (Unknown userid)\n", (unsigned int)pthread_self());
				continue;
			}
			else
			{
				dprintf(me->fd, "OK!\n");
				dprintf(destFd, "FROM %s %d %s", me->name, msgLen, pMesg);
			}
		}
		else if (strcmp(command, "SHARE") == 0)
		{
			struct ChatClient *p;
			char userId[17];
			int destFd = -1;

			printf("CHILD %u: Rcvd SHARE request\n", (unsigned int)pthread_self());
			sscanf (buffer, "%s %s %s\n", command, userId, msgLenString);
			msgLen = atoi(msgLenString);
			// traverse the list of clients to find the named recipient
			for (p = connectedClients; p != NULL; p = p->next)
			{
				if (strcmp(userId, p->name) == 0)
				{
					// found the recipient we're looking for
					destFd = p->fd;
					break;
				}
			}

			if (destFd == -1)
			{
				dprintf (me->fd, "ERROR Unknown userid\n");	// recipient not in the clients list
				printf("CHILD %u: Sent ERROR Unknown userid\n", (unsigned int)pthread_self());
			}
			else if ((me->connType != 1) || (p->connType != 1))
			{
				dprintf (me->fd, "SHARE not supported over UDP\n");	// recipient not in the clients list
			}
			else
			{
				dprintf(me->fd, "OK!\n");
				dprintf(destFd, "SHARE %s %d\n", me->name, msgLen);

				// now start the sending of the file
				while(1)
				{
					//we don't know how big the file is, keep running this loop while
					//receive 1024 byte chunks of data
					//when less than full 1024 are filled, we can assume sender is done
					//and break out of this loop
					char sendFile[1025];
					int inBytes = recv(me->fd, sendFile, 1024, 0);
					if(inBytes < 0)
					{
						perror("recv() error occured");
						exit(EXIT_FAILURE);
					}
					else if(inBytes == 0)
					{
						//connection was lost
						printf("CHILD %u: Client disconnected\n", (unsigned int)pthread_self());

						#if 0
						struct ChatClient *p, *prevNode;

						//Remove client from list of connected clients
						pthread_mutex_lock(&lock);
						for (p = prevNode = connectedClients; p != NULL; prevNode = p, p = p->next)
						{
							if (p->fd == me->fd) // found the node to be deleted
							{
								if (p == connectedClients)	// first node in the list to be removed?
								{
									connectedClients = p->next;
								}
								else
								{
									prevNode->next = p->next;
									if (p == lastClient)	// last node in the list to be deleted?
									{
										lastClient = prevNode;
									}
								}
								numClientsConnected--;
								pthread_mutex_unlock(&lock);
							}
						}
						pthread_mutex_unlock(&lock);
						close(me->fd);
						free(me);
						#endif

						return (NULL);
					}
					else if(inBytes < 1024)
					{
						//receive last of file and break loop
						//dprintf(destFd, "%s", sendFile); //send received bytes to recipient
						write(destFd, sendFile, inBytes);
						dprintf(me->fd, "OK!\n");
						break;
					}
					else if(inBytes == 1024)
					{
						//dprintf(me->fd, "TCP rcvd 1024 bytes\n");
						dprintf(me->fd, "OK!\n");
						write(destFd, sendFile, inBytes);
						//dprintf(destFd, "FROM %s %d %s\n", me->name, inBytes, sendFile); //send received bytes to recipient
						//inBytes = recv(me->fd, sendFile, 1024, 0);
					}
					else
					{
						perror("I should not be here"); //catch all for other cases
					}
				}
			}
		}
	}	// end of while (1) loop

	return (NULL);
}

void UDPclientFunction (ThreadArg *t, char *buf)
{
	char command[16];
	char argString[MAXMSG];	// holds the command arguments like the message contents
	char printBuf[MAXMSG];	// to sprintf output before sending out via sendto
	char msgLenString[4];		// holds the character string for message length
	int msgLen;			// length of the message
	struct ChatClient *me;

	if ((me = (struct ChatClient *) malloc(sizeof(struct ChatClient))) == NULL)
	{
		perror ("malloc failed ");
		exit(EXIT_FAILURE);
	}

	me->fd = t->fd;
	me->connType = 2;	// this is a UDP connection
	me->sock = t->sock;
	me->tid = pthread_self();
	me->next = (struct ChatClient *)NULL;

	bzero (command, sizeof(command));
	bzero (argString, sizeof(argString));

	sscanf (buf, "%s", command);

	if (strcmp(command, "WHO") == 0)
	{
		struct ChatClient *p;
		printf ("MAIN: Rcvd WHO request\n");
		UDPoutput (t, "OK!\n");
		for (p = connectedClients; p != NULL; p = p->next)
		{
			sprintf (printBuf, "%s\n", p->name);
			UDPoutput(t, printBuf);
		}
	}
	else if (strcmp(command, "BROADCAST") == 0)
	{
		struct ChatClient *p;
		int n;		// index into buffer string where the message content begins

		printf ("MAIN: Rcvd BROADCAST request\n");
		UDPoutput (t, "OK!\n");
		sscanf (buf, "%s %s %s", command, msgLenString, argString);
		msgLen = atoi(msgLenString);

		n = 10;		// offset at which msgLenString begins in buffer
		if (msgLen < 10)
		n += 2;
		else if (msgLen < 100)
		n += 3;
		else		// max message will be a 3-digit number
		n += 4;

		// Now n is the offset at which the message content begins
		char outp[1024];
		sprintf(outp, "FROM UDP-client %d %s", msgLen, &buf[n]);
		for (p = connectedClients; p != NULL; p = p->next)
		{
			ThreadArg tt;
			tt.fd = p->fd;
			tt.sock = t->sock;
			UDPoutput(&tt, outp);
		}
	}
}

void UDPoutput (ThreadArg *t, char *b)
{
	sendto(t->fd, b, strlen(b), 0, (struct sockaddr*) &t->sock, sizeof(t->sock));
}

int main (int argc, char *argv[])
{
	int i;
	struct sockaddr_in servaddr;
	fd_set readfds;
	struct sockaddr_in TCPclientName;
	struct sockaddr_in UDPclientName;
	socklen_t size;
	ThreadArg ta1, ta2;
	char UDPbuffer [MAXMSG];
	unsigned short portNo = 0;		// TCP & UDP will both listen on this port no
	int TCPSockFd = 0;
	int UDPSockFd = 0;

	if (argc < 2)
	{
		fprintf(stderr, "USAGE: %s <port_no>\n\n", argv[0]);
		return EXIT_FAILURE;
	}
	else if ((portNo = atoi (argv[1])) < 0)
	{
		fprintf(stderr, "port number must be a positive number\n\n");
		return EXIT_FAILURE;
	}

	setvbuf( stdout, NULL, _IONBF, 0 ); //required to make stdout unbuffered

	printf("MAIN: Started server\n");
	// Set up the TCP socket
	if ((TCPSockFd = socket(AF_INET, SOCK_STREAM , 0)) < 0)
	{
		perror("TCP socket creation failed... ");
		return EXIT_FAILURE;
	}
	bzero(&servaddr, sizeof(servaddr));

	// assign the IP and port addresses
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(portNo);

	// Bind the created socket to the given IP and port number
	if ((bind (TCPSockFd, (struct sockaddr *)&servaddr, sizeof(servaddr))) < 0)
	{
		perror("socket bind failed... ");
		return EXIT_FAILURE;
	}

	if (listen (TCPSockFd, MAX_CONNECTIONS) < 0)
	{
		fprintf(stderr, "listen failed... \n");
		return EXIT_FAILURE;
	}
	printf ("MAIN: Listening for TCP connections on port: %d\n", portNo);

	// Now setup the UDP socket
	if ((UDPSockFd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		fprintf(stderr, "UDP socket creation failed...\n");
		return EXIT_FAILURE;
	}

	// binding server addr structure to udp sockfd
	bind(UDPSockFd, (struct sockaddr*)&servaddr, sizeof(servaddr));
	printf ("MAIN: Listening for UDP datagrams on port: %d\n", portNo);

	while (1)
	{
		//printf ("MAIN: calling select()...\n");
		// Block until input arrives on one or more active sockets.
		/* Initialize the set of active sockets. */
		FD_ZERO(&readfds);
		FD_SET(TCPSockFd, &readfds);
		FD_SET(UDPSockFd, &readfds);

		if (select(FD_SETSIZE, &readfds, NULL, NULL, NULL) < 0)
		{
			fprintf(stderr, "select() failed\n");
			return EXIT_FAILURE;
		}

		// Service all the sockets with input pending.
		for (i = 0; i < FD_SETSIZE; i++)
		{
			if (FD_ISSET(i, &readfds))
			{
				//Handle TCP activity
				if (i == TCPSockFd)
				{
					// Connection request on the servers socket - must be a new connection
					int newClient;
					size = sizeof(TCPclientName);
					if ( (newClient = accept(TCPSockFd, (struct sockaddr *)&TCPclientName, (socklen_t *)&size) ) < 0 )
					{
						perror ("accept failed");
						return EXIT_FAILURE;
					}
					printf ("MAIN: Rcvd incoming TCP connection from\n");
					// Now we have an accepted connection.
					// Time to spawn a pthread to handle the application level protocol
					ta1.fd = newClient;
					ta1.sock = TCPclientName;
					if (pthread_create(&threadId[numClientsConnected], NULL, TCPclientFunction, (void *)&ta1) != 0)
					{
						perror ("pthread_create ");
						return EXIT_FAILURE;
					}
					pthread_detach(threadId[numClientsConnected]);
				}
				//Hnadle UDP activity
				if (i == UDPSockFd)
				{
					size = sizeof (UDPclientName);
					bzero (UDPbuffer, sizeof(UDPbuffer));

					recvfrom(UDPSockFd, UDPbuffer, sizeof(UDPbuffer), 0, (struct sockaddr*) &UDPclientName, &size);
					printf ("MAIN: Rcvd incoming UDP datagram from\n");
					ta2.fd = UDPSockFd;
					ta2.sock = UDPclientName;
					UDPclientFunction(&ta2, UDPbuffer);
				}
			}
		}
	} // end of while loop

	close (TCPSockFd);
	close (UDPSockFd);
}
