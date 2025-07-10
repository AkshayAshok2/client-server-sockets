#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include <filesystem>
#include "utils/utils.h"
#include "protocol/protocol.h"
#include "protocol/serialization.h"

const std::filesystem::path data_dir{"./files"};

void HandleList(unsigned int client_socket);
// void HandlePull();
// void HandleLeave();

int main(int argc, char *argv[]) {
  unsigned int server_socket;
  unsigned int client_socket;
  sockaddr_in server_address;
  sockaddr_in client_address;
  const unsigned int server_port = 9090;

  // Create a new TCP socket for incoming requests
  if ((server_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    FatalError("socket() failed");
  }

  // Construct local address structure
  server_address.sin_family      = AF_INET;
  server_address.sin_port        = htons(server_port);
  server_address.sin_addr.s_addr = htonl(INADDR_ANY);

  // Bind to local address structure
  if (bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
    FatalError("bind() failed");
  }

  // Listen for incoming connections
  if (listen(server_socket, 5) < 0) {
    FatalError("listen() failed");
  }

  std::cout << "Server is listening on port " << server_port << "\n";

  while (true) {
    // Accept incoming connection
    unsigned int client_length = sizeof(client_address);

    if ((client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_length)) < 0) {
      FatalError("accept() failed");
    }

    std::cout << "Accepted connection from " << inet_ntoa(client_address.sin_addr) << ":" << ntohs(client_address.sin_port) << "\n";

    // Inner loop to process commands on existing connection
    while (true) {
      uint8_t command;
      unsigned int bytes_received;

      if ((bytes_received = recv(client_socket, &command, 1, 0)) < 0) {
        FatalError("recv() failed");
      } else if (bytes_received == 0) {
        std::cout << "Client disconnected." << "\n";
        break;
      }

      std::cout << "Received command: " << command << "\n";

      switch (command) {
        case 1: {
          // LIST
          HandleList(client_socket);
          break;
        }
      }
    }
  }
  return 0;
};

void HandleList(unsigned int client_socket) {
  std::vector<protocol::FileHeader> files = ListFilesWithHashes(data_dir);
  protocol::ListResponse response;
  response.file_count = static_cast<uint8_t>(files.size());
  response.files = files;

  std::vector<uint8_t> serialized_response = protocol::SerializeList(response);

  uint32_t total_size = serialized_response.size();

  protocol::MessageHeader header {
    .command = protocol::Command::LIST,
    .payload_size = total_size
  };

  std::array<uint8_t, 5> serialized_header = protocol::SerializeHeader(header);
  unsigned int total_bytes_sent = 0;
  unsigned int bytes_sent;

  // Send header
  if (send(client_socket, serialized_header.data(), serialized_header.size(), 0) < 0) {
    FatalError("LIST: send() failed for message header");
  }

  while (total_bytes_sent < total_size) {
    bytes_sent = send(client_socket, serialized_response.data() + total_bytes_sent, total_size - total_bytes_sent, 0);
    if (bytes_sent < 0) {
      FatalError("send() failed");
    } else if (bytes_sent == 0) {
      FatalError("Client disconnected while sending LIST response.");
    }

    total_bytes_sent += bytes_sent;
  }

  std::cout << "LIST completed." << "\n";
}