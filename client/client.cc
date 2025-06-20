#include <iostream>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <openssl/evp.h>

// Constants
#define RECEIVE_BUFFER_SIZE 512
#define SEND_BUFFER_SIZE 512
#define MDLEN 32
#define DATA_PATH "./files"
#define FILE_READ_BUFFER_SIZE 1024
#define SERVER_PORT 9090

// Struct definitions
struct ListMessageResponse {
    uint8_t filename_bytes;
    char filename[RECEIVE_BUFFER_SIZE];
    uint8_t file_hash_bytes;
    char file_hash[RECEIVE_BUFFER_SIZE];
};

struct PullMessageResponse {
    uint8_t file_hash_bytes;
    char file_hash[RECEIVE_BUFFER_SIZE];
    uint32_t file_contents_bytes;
    char file_contents[RECEIVE_BUFFER_SIZE];
};

struct DiffMessage {
    uint8_t filename_bytes;
    char filename[RECEIVE_BUFFER_SIZE];
    uint8_t file_hash_bytes;
    char file_hash[RECEIVE_BUFFER_SIZE];
};

// Function prototypes
void FatalError(std::string message);
ListMessageResponse *List(int client_socket, uint8_t *server_file_count, char *receive_buffer);
ListMessageResponse *GetFileNamesAndHashes(uint8_t *file_count);
char *CalculateSha256Hash(const char *file_path);
DiffMessage *Diff(ListMessageResponse *server_files, uint8_t server_file_count,
                  ListMessageResponse **client_files, uint8_t cilent_file_count,
                  uint8_t diff_file_count, uint8_t suppress_output);
void ReceiveFileWithHash(int client_socket, const char *filename);
void Pull(int client_socket, uint8_t *diff_file_count, DiffMessage *diff_files, char *receive_buffer);

// Main function
int main(int argc, char *argv[]) {
  int client_socket;
  int message_length;
  struct sockaddr_in server_address;
  struct sockaddr_in client_address;

  char send_buffer[SEND_BUFFER_SIZE];
  char receive_buffer[RECEIVE_BUFFER_SIZE];
    
  int i;
  int server_port = 8080;
  char *server_ip_address = "127.0.0.1";

  uint8_t server_file_count;
  uint8_t client_file_count;
  uint8_t diff_file_count;
  bool is_diff = true;

  ListMessageResponse *server_files;
  ListMessageResponse *client_files;
  DiffMessage *diff_files;

  // Create a new TCP socket
  if ((client_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    FatalError("socket() failed");

  // Construct the server address structure
  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family        = AF_INET;
  server_address.sin_port          = htons(server_port);
  server_address.sin_addr.s_addr   = inet_addr(server_ip_address);

  // Establish connection to the server
  if (connect(client_socket, (struct sockaddr *) &server_address, sizeof(server_address)) < 0)
    FatalError("connect() failed");

  std::cout << "Welcome to MyMusic!" << std::endl;

  while (true) {
    std::cout << "\nSelect an option:\n1. LIST\n2. DIFF\n3. PULL\n4. LEAVE" << std::endl;
    uint8_t option;
    std::cin >> option;

    switch (option) {
      case 1: // LIST
        if (server_files != nullptr) {
          delete[] server_files;
        }

        server_files = List(client_socket, &server_file_count, receive_buffer);

        if (server_files == nullptr) {
          FatalError("`server_files` is null after LIST operation. Exiting.");
          break;
        } else if (server_file_count == 0) {
          FatalError("`server_file_count` is 0 after LIST operation. Exiting.");
          break;
        } 

        // Print the received files
        for (int i = 0; i < server_file_count; i++) {
          std::cout << "File: " << server_files[i].filename << std::endl;
          std::cout << "Hash: " << server_files[i].file_hash << std::endl;
        }
        
        break;
      case 2: // DIFF
        if (server_file_count == 0 || server_files == nullptr) {
          std::cout << "LIST has not been called yet. Please call that first." << std::endl;
          continue;
        }

        if (client_files != nullptr) {
          delete[] client_files;
        }

        diff_files = Diff(server_files, server_file_count, 
            &client_files, client_file_count, 
                          diff_file_count, 0);
        
        if (client_files == nullptr) {
          FatalError("`client_files` is null after DIFF operation. Exiting.");
        }
        
        break;
      case 3: // PULL
        if (client_files == nullptr) {
          std::cout << "DIFF has not been called yet. Please call that first." << std::endl;
          continue;
        }

        if (is_diff) {
          Pull(client_socket, &diff_file_count, diff_files, receive_buffer);
        } else {
          std::cout << "No files to pull." << std::endl;
        }

        break;
      case 4: // LEAVE
        if (send(client_socket, &option, sizeof(option), 0) != sizeof(option)) {
          FatalError("`send()` sent unexpected number of bytes.");
        }

        if (server_files != nullptr) {
          delete[] server_files;
        }
        if (client_files != nullptr) {
          delete[] client_files;
        }
        if (diff_files != nullptr) {
          delete[] diff_files;
        }

        close(client_socket);
        return 0;
      default:
        std::cout << "Invalid option. Please try again." << std::endl;
    }
  }

  return 0;
}

void FatalError(std::string message) {
  perror(message.c_str());
  exit(1);
}

ListMessageResponse *List(int client_socket, uint8_t *server_file_count, char *receive_buffer) {
  if (receive_buffer != nullptr) {
    delete[] receive_buffer;
  }

  uint8_t option = 1;

  if (send(client_socket, &option, sizeof(option), 0) != sizeof(option)) {
    FatalError("`send()` sent unexpected number of bytes.");
  }

  uint8_t bytes_received = recv(client_socket, server_file_count, sizeof(*server_file_count), 0);

  if (bytes_received < 0) {
    FatalError("First `recv()` failed.");
  } else if (bytes_received == 0) {
    FatalError("Server closed connection during LIST operation.");
  }

  uint32_t total_message_size = *server_file_count *sizeof(uint8_t) + 2 * RECEIVE_BUFFER_SIZE;

  if (receive_buffer != nullptr) {
    FatalError("`malloc` failed to allocate memory for `receive_buffer`.");
  }

  memset(receive_buffer, 0, total_message_size);
  int total_bytes_received = 0;

  while (total_bytes_received < total_message_size) {
    bytes_received = recv(client_socket, receive_buffer + total_bytes_received,
                          total_message_size - total_bytes_received, 0);
    
    if (bytes_received < 0) {
      FatalError("`recv()` failed during LIST operation for file names and hashes.");
    } else if (bytes_received == 0) {
      FatalError("Server closed connection during LIST operation. Stopping here.");
      break;
    }

    total_bytes_received += bytes_received;
  }

  ListMessageResponse *server_files = (ListMessageResponse *)malloc(*server_file_count * sizeof(ListMessageResponse));

}