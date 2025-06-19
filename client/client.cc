#include <stdio.h>
#include <sys.socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <openssl/evp.h>

#define RCVBUFSIZE 512		    /* The receive buffer size */
#define SNDBUFSIZE 512		    /* The send buffer size */
#define MDLEN 32

void fatal_error(char *message) {
  perror(message);
  exit(1);
}

int main(int argc, char *argv[]) {
  int clientSock;		    /* socket descriptor */
  int msgLen;
  struct sockaddr_in serv_addr;   /* The server address */
  struct sockaddr_in client_addr; /* The client address */

  char *studentName;		    /* Your Name */
  char sndBuf[SNDBUFSIZE];	    /* Send Buffer */
  char rcvBuf[RCVBUFSIZE];	    /* Receive Buffer */
    
  int i;			    /* Counter Value */
  int servPort = 8080;
  char *servIP = "127.0.0.1";

  /* Get the Student Name from the command line */
  if (argc != 2) 
  {
    printf("Incorrect input format. The correct format is:\n\tnameChanger your_name\n");
        exit(1);
    }
    studentName = argv[1];
    memset(&sndBuf, 0, RCVBUFSIZE);
    memset(&rcvBuf, 0, RCVBUFSIZE);

    msgLen = strlen(studentName);
    strncpy(sndBuf, studentName, msgLen);
    sndBuf[msgLen] = '\0';

    /* Create a new TCP socket*/
    if ((clientSock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
        fatal_error("socket() failed");

    /* Construct the server address structure */
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family        = AF_INET;
    serv_addr.sin_port          = htons(servPort);
    serv_addr.sin_addr.s_addr   = inet_addr(servIP);

    /* Establish connection to the server */
    if (connect(clientSock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
        fatal_error("connect() failed");
    
    /* Send the string to the server */
    if (send(clientSock, sndBuf, msgLen, 0) != msgLen)
        fatal_error("send() sent unexpected number of bytes");

    /* Receive and print response from the server */
    int receivedBytes = 1;
    while (receivedBytes > 0) {
        if ((receivedBytes = recv(clientSock, rcvBuf, RCVBUFSIZE - 1, 0)) < 0) {
            fatal_error("recv() failed");
        }
    }

    close(clientSock);
    rcvBuf[receivedBytes] = '\0';

    printf("%s\n", studentName);
    printf("Transformed input is: ");
    for(i = 0; i < MDLEN; i++) printf("%02x", rcvBuf[i]);
    printf("\n");

    return 0;
}