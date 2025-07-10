#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include "../hash/sha256.h"

// Constants
constexpr int kReceiveBufferSize = 512;
constexpr int kSendBufferSize = 512;
constexpr const char *kDataDir = "./files";
constexpr int kFileReadBufferSize = 1024;
constexpr int kSha256HexLen = 64;
constexpr int kServerPort = 9090;

enum class State { Fresh, Listed, Diffed, Pulled };

class ClientApp {
 public:
  void run();
 private:
  void DoList();
  void DoDiff();
  void DoPull();
  void DoLeave();

  State state_ = State::Fresh;
  std::vector<FileEntry> server_files_;
  std::vector<FileEntry> client_files_;
  std::vector<DiffEntry> diff_files_;
}

// Function prototypes
ListMessageResponse *List(int client_socket, uint8_t *server_file_count, char *receive_buffer);
ListMessageResponse *GetFileNamesAndHashes(uint8_t *file_count);
std::string CalculateSha256Hash(const std::string& file_path);
DiffMessage *Diff(ListMessageResponse *server_files, uint8_t server_file_count,
                  ListMessageResponse **client_files, uint8_t cilent_file_count,
                  uint8_t diff_file_count, uint8_t suppress_output);
void ReceiveFileWithHash(int client_socket, const char *file_name);
void Pull(int client_socket, uint8_t *diff_file_count, DiffMessage *diff_files, char *receive_buffer);

// Main function
int main(int argc, char *argv[]) {
  int client_socket;
  int message_length;
  sockaddr_in server_address;
  sockaddr_in client_address;

  char *send_buffer;
  char *receive_buffer;
    
  std::string server_ip_address = "127.0.0.1";

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
  server_address.sin_family        = AF_INET;
  server_address.sin_port          = htons(kServerPort);
  server_address.sin_addr.s_addr   = inet_addr(server_ip_address.c_str());

  // Establish connection to the server
  if (connect(client_socket, &server_address, sizeof(server_address)) < 0)
    FatalError("connect() failed");

  std::cout << "Welcome to MyMusic!" << "\n";

  while (true) {
    std::cout << "\nSelect an option:\n1. LIST\n2. DIFF\n3. PULL\n4. LEAVE" << "\n";
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
          std::cout << "File: " << server_files[i].file_name << "\n";
          std::cout << "Hash: " << server_files[i].file_hash << "\n\n";
        }
        
        break;
      case 2: // DIFF
        if (server_file_count == 0 || server_files == nullptr) {
          std::cout << "LIST has not been called yet. Please call that first." << "\n";
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
          std::cout << "DIFF has not been called yet. Please call that first." << "\n";
          continue;
        }

        if (is_diff) {
          Pull(client_socket, &diff_file_count, diff_files, receive_buffer);
        } else {
          std::cout << "No files to pull." << "\n";
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
        std::cout << "Invalid option. Please try again." << "\n";
    }
  }

  return 0;
}

void FatalError(const std::string& message) {
  perror(message.c_str());
  exit(1);
}

ListMessageResponse *List(int client_socket, uint8_t *server_file_count, char *receive_buffer) {
  if (receive_buffer != nullptr) {
    delete[] receive_buffer;
  }

  uint8_t option = 1;

  if (send(client_socket, &option, 1, 0) != 1) {
    FatalError("`send()` sent unexpected number of bytes.");
  }

  uint8_t bytes_received = recv(client_socket, server_file_count, sizeof(*server_file_count), 0);

  if (bytes_received < 0) {
    FatalError("First `recv()` failed.");
  } else if (bytes_received == 0) {
    FatalError("Server closed connection during LIST operation.");
  }

  uint32_t total_message_size = *server_file_count *sizeof(uint8_t) + 2 * kReceiveBufferSize;

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

  ListMessageResponse *server_files = (ListMessageResponse *) malloc(*server_file_count * sizeof(ListMessageResponse));

  if (server_files == nullptr) {
    FatalError("`malloc` failed to allocate memory for `server_files`.");
  }

  memset(server_files, 0, *server_file_count * sizeof(ListMessageResponse));
  int offset = 0;

  // Deserialize each ListMessageResponse
  for (int i = 0; i < *server_file_count; i++) {
    ListMessageResponse *response = &server_files[i];

    // Deserialize filename_bytes
    memcpy(&response->file_name_bytes, receive_buffer + offset, 1);
    offset++;

    // Deserialize filename
    memcpy(response->file_name, receive_buffer + offset, response->file_name_bytes);
    offset += kReceiveBufferSize;

    // Deserialize file_hash
    memcpy(response->file_hash, receive_buffer + offset, kSha256HexLen + 1);
    offset += kReceiveBufferSize;
  }

  delete receive_buffer;
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

// Returns populated list of ListMessageResponse objects
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
      uint8_t file_hash_bytes = (uint8_t) (file_hash.length() + 1); // +1 for null terminator

      if (file_hash.length() > 0) {
        for (char c : file_hash) {
          file_infos[current_file_count].file_hash[current_file_count] = c;
        }
        file_infos[current_file_count].file_hash[file_hash_bytes - 1] = '\0';
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

// Function to handle DIFF command
DiffMessage *Diff(ListMessageResponse *server_files,
                  uint8_t server_file_count,
                  ListMessageResponse **client_files,
                  uint8_t *client_file_count,
                  uint8_t *diff_file_count,
                  uint8_t suppress_output) {
  *client_files = GetFileNamesAndHashes(&client_file_count);

  if (*client_file_count == 0) {
    std::cout << "No files found on the client. Exiting...\n";
    return nullptr;
  }

  DiffMessage *diff_files = nullptr;

  if (*client_files == nullptr || server_files == nullptr) {
    FatalError("`client_files` or `server_files` is null in Diff function.");
  }

  if (!suppress_output) {
    std::cout << "Files on both machines:\n";
    for (int i = 0; i < server_file_count; i++) {
      for (int j = 0; j < *client_file_count; j++) {
        if (strcmp(server_files[i].file_hash, (*client_files)[j].file_hash) == 0) {
          std::cout << "File: " << server_files[i].file_name << "\n";
        }
      }
    }
  }

  *diff_file_count = server_file_count - *client_file_count;

  if (*diff_file_count > 0) {
    if (!suppress_output) {
      std::cout << "Files missing on the client:\n";
    }

    diff_files = (DiffMessage *) malloc(*diff_file_count * sizeof(DiffMessage));

    if (diff_files == nullptr) {
      FatalError("malloc() failed for diff_files");
    }

    memset(diff_files, 0, *diff_file_count * sizeof(DiffMessage));
    int diff_count = 0;

    for (int i = 0; i < server_file_count; i++) {
      bool found = false;

      for (int j = 0; j < *client_file_count; j++) {
        if (strcmp(server_files[i].file_hash, (*client_files)[j].file_hash) == 0) {
          found = true;
          break;
        }
      }

      if (!found) {
        if (!suppress_output) {
          std::cout << "File: " << server_files[i].file_name << "\n";
        }

        strncpy(diff_files[diff_count].file_name, server_files[i].file_name, kReceiveBufferSize);
        diff_files[diff_count].file_name_bytes = server_files[i].file_name_bytes;
        diff_files[diff_count].file_name[kReceiveBufferSize - 1] = '\0'; // Ensure null termination
        
        strncpy(diff_files[diff_count].file_hash, server_files[i].file_hash, kSha256HexLen + 1);
        diff_files[diff_count].file_hash[kSha256HexLen] = '\0'; // Ensure null termination

        diff_count++;
      }
    }
  } else {
    std::cout << "No files missing on the client.\n";
  }

  return diff_files;
}

// Function to handle PULL command
void Pull(int client_socket, uint8_t *diff_file_count, DiffMessage *diff_files, char *receive_buffer) {
  uint8_t option = 3;
  char *buffer;

  // Send the PULL command to the server
  if (send(client_socket, &option, 1, 0) != 1) {
    FatalError("`send()` sent unexpected number of bytes for PULL command.");
  }

  // Send the number of files to be pulled
  if (send(client_socket, diff_file_count, 1, 0) != 1) {
    FatalError("`send()` sent unexpected number of bytes for diff_file_count.");
  }

  // Validate inputs
  if (diff_file_count == nullptr || *diff_file_count == 0) {
    FatalError("Invalid diff_file_count");
  }

  if (diff_files == nullptr) {
    FatalError("Invalid diff_files pointer");
  }

  size_t total_size = *diff_file_count * (2 + 2 * kReceiveBufferSize);
  std::cout << "Total size of diff files: " << total_size << " bytes\n";
  buffer = (char *) malloc(total_size);

  if (buffer == nullptr) {
    FatalError("malloc() failed for buffer");
  }

  memset(buffer, 0, total_size);

  // Serialize the data
  int offset = 0;

  for (int i = 0; i < *diff_file_count; i++) {
    memcpy(buffer + offset, &diff_files[i].file_name_bytes, 1);
    offset++;
    memcpy(buffer + offset, diff_files[i].file_name, diff_files[i].file_name_bytes);
    offset += kReceiveBufferSize;
    memcpy(buffer + offset, diff_files[i].file_hash, kSha256HexLen + 1);
    offset += kSha256HexLen + 1;
  }

  // Send the data
  int total_bytes_sent = 0;
  int bytes_sent;

  while (total_bytes_sent < total_size) {
    bytes_sent = send(client_socket, buffer + total_bytes_sent, total_size - total_bytes_sent, 0);

    if (bytes_sent < 0) {
      FatalError("send() failed during PULL operation");
    } else if (bytes_sent == 0) {
      FatalError("Server closed connection during PULL operation.");
    }

    total_bytes_sent += bytes_sent;
  }

  free(buffer);
  buffer = nullptr;
  uint8_t server_diff_file_count;

  // Receive the number of files being sent from the server
  if (recv(client_socket, &server_diff_file_count, 1, 0) != 1
      || server_diff_file_count != *diff_file_count) {
    FatalError("recv() for diff_file_count received unexpected number of bytes or wrong value");
  }

  // Receive all files
  for (int i = 0; i < server_diff_file_count; i++) {
    int total_bytes_received = 0;
    int bytes_recieved = 0;
    char *file_header;
    char *file_contents;

    uint8_t file_name_bytes;
    char *file_name;
    uint32_t file_contents_bytes;

    // Receive header fields
    int header_bytes = 1 + kReceiveBufferSize + 4; // uint8_t + kReceiveBufferSize + uint32_t
    file_header = (char *) malloc(header_bytes);

    if (file_header == nullptr) {
      FatalError("malloc() failed for file_header");
    }

    memset(file_header, 0, header_bytes);

    while (total_bytes_received < header_bytes) {
      bytes_recieved = recv(client_socket, file_header + total_bytes_received,
                            header_bytes - total_bytes_received, 0);

      if (bytes_recieved < 0) {
        FatalError("recv() failed during PULL operation for file header");
      } else if (bytes_recieved == 0) {
        FatalError("Server closed connection during PULL operation.");
      }

      total_bytes_received += bytes_recieved;
    }

    std::cout << "Ideal bytes received: " << header_bytes << "\n";
    std::cout << "Total bytes received: " << total_bytes_received << "\n";

    // Deserialize header fields
    offset = 0;
    memcpy(&file_name_bytes, file_header + offset, 1);
    offset++;

    file_name = (char *) malloc(file_name_bytes);

    if (file_name == nullptr) {
      free(file_header);
      FatalError("malloc() failed for file_name");
    }

    memset(file_name, 0, file_name_bytes);
    memcpy(file_name, file_header + offset, file_name_bytes);
    offset += kReceiveBufferSize;

    memcpy(&file_contents_bytes, file_header + offset, 4);

    // Very header fields
    std::cout << "File name bytes: " << (int) file_name_bytes << "\n";
    std::cout << "File name: " << file_name << "\n";
    std::cout << "File contents bytes: " << file_contents_bytes << "\n";

    // Receive file contents
    file_contents = (char *) malloc(file_contents_bytes);

    if (file_contents == nullptr) {
      free(file_header);
      free(file_name);
      FatalError("malloc() failed for file_contents");
    }

    memset(file_contents, 0, file_contents_bytes);
    total_bytes_received = 0;

    while (total_bytes_received < file_contents_bytes) {
      bytes_recieved = recv(client_socket, file_contents + total_bytes_received,
                            file_contents_bytes - total_bytes_received, 0);

      if (bytes_recieved < 0) {
        free(file_header);
        free(file_name);
        free(file_contents);
        FatalError("recv() failed during PULL operation for file contents");
      } else if (bytes_recieved == 0) {
        free(file_header);
        free(file_name);
        free(file_contents);
        FatalError("Server closed connection during PULL operation.");
      }

      total_bytes_received += bytes_recieved;
    }

    // Write to file
    char file_path[kReceiveBufferSize];
    snprintf(file_path, sizeof(file_path), "%s/%s", kDataDir, file_name);
    FILE *fp = fopen(file_path, "wb");

    if (fp == nullptr) {
      free(file_header);
      free(file_name);
      free(file_contents);
      FatalError("fopen() failed for file: " + std::string(file_path));
    }

    if (fwrite(file_contents, 1, file_contents_bytes, fp) != file_contents_bytes) {
      fclose(fp);
      free(file_header);
      free(file_name);
      free(file_contents);
      FatalError("fwrite() failed for file: " + std::string(file_path));
    }

    fclose(fp);
    free(file_header);
    free(file_name);
    free(file_contents);
    file_header = nullptr;
    file_name = nullptr;
    file_contents = nullptr;
  }

  free(buffer);
  buffer = nullptr;
  return;
}
