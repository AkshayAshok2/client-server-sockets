#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <string>
#include <openssl/evp.h>

#define RECEIVE_BUFFER_SIZE 512 // The receive buffer size
#define SEND_BUFFER_SIZE 512    // The send buffer size
#define BUFFER_SIZE 40		    // Your name can be as many as 40 chars

void fatal_error(char *message)
{
  perror(message);
  exit(1);
}

int main(int argc, char *argv[])
{
  int server_socket;
  int client_socket;
  struct sockaddr_in change_server_address;
  struct sockaddr_in change_client_address;
  unsigned short change_client_port;
  unsigned short change_server_port;
  unsigned int client_len;

  char name_buf[BUFFER_SIZE];               // Buffer to store name from client
  unsigned char md_value[EVP_MAX_MD_SIZE];	// Buff to store change result
  EVP_MD_CTX *mdctx;				        // Digest data structure declaration
  const EVP_MD *md;				            // Digest data structure declaration
  int md_len;					            // Digest data structure size tracking

  change_server_port = 9090;

  // Create new TCP Socket for incoming requests
  if ((server_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    fatal_error("socket() failed");

  // Construct local address structure
  memset(&change_server_address, 0, sizeof(change_server_address));
  change_server_address.sin_family       = AF_INET;
  change_server_address.sin_addr.s_addr  = htonl(INADDR_ANY);
  change_server_address.sin_port         = htons(change_server_port);
  
  // Bind to local address structure
  if (bind(server_socket, (struct sockaddr *) &change_server_address, sizeof(change_server_address)) < 0)
    fatal_error("bind() failed");

  // Listen for incoming connections
  if (listen(server_socket, 5) < 0)
    fatal_error("listen() failed");

  // Loop server forever
  while(1)
  {
    // Accept incoming connection
    client_len = sizeof(change_client_address);
    if ((client_socket = accept(server_socket, (struct sockaddr *) &change_client_address, &client_len)) < 0)
      fatal_error("accept() failed");

    // Extract Your Name from the packet, store in name_buf
    int bytes_received;
    if ((bytes_received = recv(client_socket, name_buf, RECEIVE_BUFFER_SIZE, 0)) < 0)
      fatal_error("recv() failed");

    // Run this and return the final value in md_value to client
    // Takes the client name and changes it
    // Students should NOT touch this code
    OpenSSL_add_all_digests();
    md = EVP_get_digestbyname("SHA256");
    mdctx = EVP_MD_CTX_create();
    EVP_DigestInit_ex(mdctx, md, NULL);
    EVP_DigestUpdate(mdctx, name_buf, strlen(name_buf));
    EVP_DigestFinal_ex(mdctx, md_value, &md_len);
    EVP_MD_CTX_destroy(mdctx);

    // Return md_value to client */
    if (send(client_socket, md_value, sizeof(md_value), 0) < 0)
      fatal_error("send() failed");

    close(client_socket);
  }

  close(server_socket);
  return 0;
}