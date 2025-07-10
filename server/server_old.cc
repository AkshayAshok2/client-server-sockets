#include <cstdint>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstdlib>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <iostream>
#include <cstring>
#include <cstdio>
#include <string>
#include <array>
#include "../hash/sha256.h"

constexpr int kReceiveBufferSize = 512;
constexpr int kSendBufferSize = 512;
constexpr int kBufferSize = 40;
constexpr const char *kDataDir = "./files";
constexpr int kFileReadBufferSize = 1024;
constexpr int kSha256HexLen = 64;
constexpr int kServerPort = 9090;

// Function prototypes
ListMessageResponse* GetFileNamesAndHashes(uint8_t* file_count);
void SendSingleFile(int client_socket, const std::string& file_name, uint8_t file_name_bytes);
std::string CalculateSha256(const std::string& file_path);

int main(int argc, char *argv[]) {
  uint8_t file_count;
  int server_socket = 0;
  int client_socket = 0;
  sockaddr_in server_address;
  sockaddr_in client_address;
  unsigned int client_length = 0;

  char *receive_buffer;

  ListMessageResponse *server_files;

  // Create new TCP Socket for incoming requests
  if ((server_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    FatalError("socket() failed");

  // Construct local address structure
  server_address.sin_family       = AF_INET;
  server_address.sin_addr.s_addr  = htonl(INADDR_ANY);
  server_address.sin_port         = htons(kServerPort);
  
  // Bind to local address structure
  if (bind(server_socket, (struct sockaddr *) &server_address, sizeof(server_address)) < 0)
    FatalError("bind() failed");

  // Listen for incoming connections
  if (listen(server_socket, 5) < 0)
    FatalError("listen() failed");

  // Outer loop to handle new incoming connections
  while(true) {
    // Accept incoming connection
    client_length = sizeof(client_address);

    if ((client_socket = accept(server_socket, (struct sockaddr *) &client_address, &client_length)) < 0)
      FatalError("accept() failed");

    // Inner loop to process commands on existing connection
    while (true) {
      int bytes_received;
      uint8_t command;

      if ((bytes_received = recv(client_socket, &command, 1, 0)) < 0) {
        FatalError("recv() failed");
      } else if (bytes_received == 0) {
        FatalError("Client disconnected.\n");
      }

      std::cout << "Received menu choice: " << command << "\n";

      switch (command) {
        case 1: {
          // LIST
          server_files = GetFileNamesAndHashes(&file_count);

          if (server_files == nullptr) {
            FatalError("GetFileNamesAndHashes() failed");
          }

          int total_size = 1 + file_count * (2 + 2 * kReceiveBufferSize);
          char *buffer = (char *) malloc(total_size);

          if (buffer == nullptr) {
            FatalError("malloc() failed for buffer");
          }

          memset(buffer, 0, total_size);

          // Serialize the data
          int offset = 0;
          memcpy(buffer, &file_count, 1);
          offset ++;

          for (int i = 0; i < file_count; i++) {
            memcpy(buffer + offset, &server_files[i].file_name_bytes, 1);
            offset ++;
            memcpy(buffer + offset, server_files[i].file_name, kReceiveBufferSize);
            offset += kReceiveBufferSize;
            memcpy(buffer + offset, server_files[i].file_hash, kReceiveBufferSize);
            offset += kReceiveBufferSize;
          }

          // Send the data
          int total_bytes_sent = 0;
          int bytes_sent;

          while (total_bytes_sent < total_size) {
            bytes_sent = send(client_socket, buffer + total_bytes_sent, total_size - total_bytes_sent, 0);

            if (bytes_sent < 0) {
              FatalError("send() failed");
            } else if (bytes_sent == 0) {
              FatalError("Server closed connection during LIST operation.");
            }

            total_bytes_sent += bytes_sent;
          }

          delete[] buffer;
          break;
        }
        case 3: {
          // PULL
          uint8_t diff_file_count;
          int bytes_received = recv(client_socket, &diff_file_count, 1, 0);

          if (bytes_received < 0) {
            FatalError("recv() failed for diff_file_count");
          } else if (bytes_received == 0) {
            FatalError("Client disconnected during PULL operation.");
          }

          std::cout << "Number of files to pull: " << diff_file_count << "\n";

          uint32_t total_message_size = diff_file_count * (2 + 2 * kReceiveBufferSize);
          
          if (receive_buffer != nullptr) {
            delete[] receive_buffer;
          }

          receive_buffer = (char *) malloc(total_message_size);

          if (receive_buffer == nullptr) {
            FatalError("malloc() failed for receive_buffer");
          }

          memset(receive_buffer, 0, total_message_size);
          int total_bytes_received = 0;

          // Recieve DIFF from client
          while (total_bytes_received < total_message_size) {
            bytes_received = recv(client_socket, receive_buffer + total_bytes_received, total_message_size - total_bytes_received, 0);

            if (bytes_received < 0) {
              FatalError("recv() failed during DIFF operation for file names and hashes.");
            } else if (bytes_received == 0) {
              FatalError("Server closed connection during PULL operation. Stopping here.");
            }

            total_bytes_received += bytes_received;
          }

          DiffMessage *diff_files = (DiffMessage *) malloc(diff_file_count * sizeof(DiffMessage));

          if (diff_files == nullptr) {
            FatalError("malloc() failed for diff_files");
          }

          memset(diff_files, 0, total_message_size);
          int offset = 0;

          // Deserialize each ListMessageResponse
          for (int i = 0; i < diff_file_count; i++) {
            DiffMessage *diff_file = &diff_files[i];

            // Deserialize file_name_bytes
            memcpy(&diff_file->file_name_bytes, receive_buffer + offset, 1);
            offset++;

            // Deserialize file_name
            memcpy(diff_file->file_name, receive_buffer + offset, diff_file->file_name_bytes);
            offset += kReceiveBufferSize;

            // Deserialize file_hash
            memcpy(diff_file->file_hash, receive_buffer + offset, kSha256HexLen + 1);
            offset += kReceiveBufferSize;
          }

          delete[] receive_buffer;

          // Send number of files being sent to client
          if (send(client_socket, &diff_file_count, 1, 0) != 1) {
            FatalError("send() failed for diff_file_count: unexpected number of bytes");
          }

          // Send all files with hashes to the client
          for (int i = 0; i < diff_file_count; i++) {
            SendSingleFile(client_socket, diff_files[i].file_name, diff_files[i].file_name_bytes);
          }

          delete[] diff_files;
          break;
        }
        case 4: {
          // LEAVE
          if (server_files != nullptr) {
            delete[] server_files;
          }

          close(client_socket);
          break;
        }
        default:
          std::cout << "Client sent invalid option. Exiting..." << "\n";
          close(client_socket);
          break;
      }
    }
  }

  close(server_socket);
  return 0;
}

void FatalError(const std::string& message) {
  std::perror(message.c_str());
  std::exit(1);
}

// Returns null-terminated SHA-256 hash
std::string CalculateSha256(const std::string& file_path) {
  FILE *file = fopen(file_path.c_str(), "rb");

  if (!file) {
    FatalError("fopen() failed for file: " + std::string(file_path));
  }

  SHA256 sha256;

  // Read the file in chunks and update the hash
  unsigned char buffer [kFileReadBufferSize];
  size_t bytes_read;

  while ((bytes_read = fread(buffer, 1, kFileReadBufferSize, file)) > 0) {
    sha256.add(buffer, bytes_read);
  }

  // Convert hash to hexadecimal
  std::string hash_string = sha256.getHash();
  return hash_string;
}

// Get file names
ListMessageResponse *GetFileNamesAndHashes(uint8_t *file_count) {
  DIR *current_dir;
  struct dirent *entry;
  char file_path[1024];
  int capacity = 10; // init capacity for storing file info
  uint8_t current_file_count = 0;

  ListMessageResponse *file_infos = (ListMessageResponse *) malloc(capacity * sizeof(ListMessageResponse));

  if (file_infos == nullptr) {
    FatalError("malloc() failed for file_infos");
  }

  memset(file_infos, 0, capacity * sizeof(ListMessageResponse));

  if ((current_dir = opendir(kDataDir)) == nullptr) {
    delete[] file_infos;
    file_infos = nullptr;
    FatalError("opendir() failed for directory: " + std::string(kDataDir));
  }

  // Loop thru each entry in the directory
  while ((entry = readdir(current_dir)) != nullptr) {
    snprintf(file_path, sizeof(file_path), "%s/%s", kDataDir, entry->d_name);
    struct stat file_stat;

    if (stat(file_path, &file_stat) == 0 && S_ISREG(file_stat.st_mode)) {
      // Double size if capacity reached
      if (current_file_count >= capacity) {
        int new_capacity = capacity * 2;
        ListMessageResponse *temp = realloc(file_infos, new_capacity * sizeof(ListMessageResponse));

        if (temp == nullptr) {
          delete[] file_infos;
          closedir(current_dir);
          FatalError("realloc() failed for file_infos");
        }

        memset(temp + capacity, 0, capacity * sizeof(ListMessageResponse));
        file_infos = temp;
        capacity = new_capacity;
      }

      // Copy file name into struct
      uint8_t file_name_bytes = (uint8_t) (strlen(entry->d_name) + 1); // +1 for null terminator
      strncpy(file_infos[current_file_count].file_name, entry->d_name, file_name_bytes);
      file_infos[current_file_count].file_name_bytes = file_name_bytes;

      // Calculate and copy SHA-256 hash
      std::string file_hash = CalculateSha256(file_path);
      uint8_t file_hash_bytes = (uint8_t) (strlen(file_hash) + 1); // +1 for null terminator

      if (file_hash.length() > 0) {
        file_infos[current_file_count].file_hash = *file_hash.c_str();
      } else {
        std::cout << "Failed to calculate hash for file: " << entry->d_name << "\n";
        continue;
      }

      current_file_count++;
    }
  }

  *file_count = current_file_count;
  closedir(current_dir);
  return file_infos;
}

// Send a single file to the client
void SendSingleFile(int client_socket, const char *file_name, uint8_t file_name_bytes) {
  char *send_buffer;
  char file_path[kReceiveBufferSize];

  snprintf(file_path, sizeof(file_path), "%s/%s", kDataDir, file_name);
  int file_fd = open(file_path, O_RDONLY);

  if (file_fd < 0) {
    FatalError("open() failed for file: ");
  }

  struct stat file_stat;

  if (fstat(file_fd, &file_stat) < 0) {
    close(file_fd);
    FatalError("fstat() failed for file: ");
  }

  uint32_t file_size = file_stat.st_size;
  std::cout << "File size: " << file_size << " bytes\n";

  // Prepare buffer
  int header_message_size = 1 + kReceiveBufferSize + 4; // 1 byte for file_name_bytes, kReceiveBufferSize for file_name, 4 bytes for file_contents_bytes
  send_buffer = (char *) malloc(header_message_size);

  if (send_buffer == nullptr) {
    close(file_fd);
    FatalError("malloc() failed for send_buffer");
  }

  memset(send_buffer, 0, header_message_size);

  // Write file_name_bytes, file_name, and file_contents_bytes to the buffer
  int offset = 0;
  memcpy(send_buffer, &file_name_bytes, 1);
  offset++;
  memcpy(send_buffer + offset, file_name, file_name_bytes);
  offset += kReceiveBufferSize;
  memcpy(send_buffer + offset, &file_size, 4);

  // Send file header fields to client
  int total_bytes_sent = 0;
  int bytes_sent;

  while (total_bytes_sent < header_message_size) {
    bytes_sent = send(client_socket, send_buffer + total_bytes_sent, header_message_size - total_bytes_sent, 0);

    if (bytes_sent < 0) {
      FatalError("send() failed for file header");
    } else if (bytes_sent == 0) {
      FatalError("Server closed connection during PULL operation.");
    }

    total_bytes_sent += bytes_sent;
  }

  free(send_buffer);

  // Prepare buffer for file contents
  send_buffer = (char *) malloc(file_size);

  if (send_buffer == nullptr) {
    close(file_fd);
    FatalError("malloc() failed for send_buffer");
  }

  memset(send_buffer, 0, file_size);

  // Read file contents into buffer
  int total_bytes_read = 0;
  int bytes_read;

  while (total_bytes_read < file_size) {
    if ((bytes_read = read(file_fd, send_buffer + total_bytes_read, file_size - total_bytes_read)) > 0) {
      total_bytes_read += bytes_read;
    } else if (bytes_read < 0) {
      close(file_fd);
      free(send_buffer);
      FatalError("read() failed for file contents");
    } else if (bytes_read == 0 && total_bytes_read < file_size) {
      free(send_buffer);
      close(file_fd);
      FatalError("read() ended with total_bytes_read < file_size");
    }
  }

  std::cout << "Total bytes read: " << total_bytes_read << "\n";

  if (bytes_read < 0) {
    close(file_fd);
    free(send_buffer);
    FatalError("read() failed for file contents");
  }

  close(file_fd);

  // Send file contents to client
  total_bytes_sent = 0;

  while (total_bytes_sent < file_size) {
    bytes_sent = send(client_socket, send_buffer + total_bytes_sent, file_size - total_bytes_sent, 0);

    if (bytes_sent < 0) {
      free(send_buffer);
      FatalError("send() failed for file contents");
    } else if (bytes_sent == 0) {
      free(send_buffer);
      FatalError("Server closed connection during PULL operation.");
    }

    total_bytes_sent += bytes_sent;
  }

  std::cout << "Total bytes sent: " << total_bytes_sent << "\n";
  free(send_buffer);
}