#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <vector>
#include <filesystem>
#include "utils/utils.h"
#include "protocol/protocol.h"
#include "protocol/serialization.h"

void ServeClient(int client_socket, const std::filesystem::path& data_dir);
void HandleList(int client_socket, const std::filesystem::path& data_dir);
void HandlePull(int client_socket, uint32_t payload_size, const std::filesystem::path& data_dir);
void HandleLeave(int cilent_socket);

int main(int argc, char *argv[]) {
  int server_socket;
  int client_socket;
  sockaddr_in server_address;
  sockaddr_in client_address;
  const unsigned int server_port = 9090;

  // Initialize data directory
  std::filesystem::path data_dir = std::filesystem::current_path() / "server" / "files";
  std::cout << "Data directory: " << data_dir << "\n";

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

    // Process commands on existing connection
    ServeClient(client_socket, data_dir);
  }
  return 0;
};

void ServeClient(int client_socket, const std::filesystem::path& data_dir) {
  while (true) {
    std::array<uint8_t, 5> header_buffer;
    int bytes_received = recv(client_socket, header_buffer.data(), header_buffer.size(), MSG_WAITALL);

    if (bytes_received < 0) {
      FatalError("recv() failed");
    } else if (bytes_received == 0) {
      std::cout << "Client disconnected" << "\n";
      return;
    } else if (bytes_received < static_cast<int>(header_buffer.size())) {
      FatalError("Received incomplete header, expected " + std::to_string(header_buffer.size()) + " bytes, got " + std::to_string(bytes_received) + " bytes.");
    }

    int command = header_buffer[0];
    uint32_t payload_size = ntohl(*reinterpret_cast<uint32_t*>(header_buffer.data() + 1));

    std::cout << "Received command: " << command << "\n";

    switch (command) {
      case 1: {
        // LIST
        HandleList(client_socket, data_dir);
        break;
      }
      case 3: {
        // PULL
        HandlePull(client_socket, payload_size, data_dir);
        break;
      }
      case 4: {
        // LEAVE
        HandleLeave(client_socket);
        return;
      }
      default:
        std::cout << "Unknown command received: " << command << "\n";
    }
  }
}

void HandleList(int client_socket, const std::filesystem::path& data_dir) {
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

void HandlePull(int client_socket, uint32_t payload_size, const std::filesystem::path& data_dir) {
  // Receive PULL request payload from client
  unsigned int total_bytes_received = 0;
  int bytes_received;
  std::vector<uint8_t> receive_buffer(payload_size);

  while (total_bytes_received < payload_size) {
    bytes_received = recv(client_socket, receive_buffer.data() + total_bytes_received, payload_size - total_bytes_received, 0);
    
    if (bytes_received < 0) {
      FatalError("recv() failed");
    } else if (bytes_received == 0) {
      FatalError("Client disconnected while receiving PULL request.");
    }

    total_bytes_received += bytes_received;
  }

  protocol::PullRequest request = protocol::DeserializePullRequest(receive_buffer);

  // Prepare PULL request response
  for (const auto& file : request.files) {
    std::vector<uint8_t> file_bytes = ReadFileBytes(data_dir / file.name);
    protocol::FileContents file_contents {
      .header = file,
      .size = static_cast<uint32_t>(file_bytes.size()),
      .bytes = std::move(file_bytes)
    };
    std::vector<uint8_t> send_buffer = protocol::SerializeFileContents(file_contents);

    // Send response header
    protocol::MessageHeader header {
      .command = protocol::Command::PULL,
      .payload_size = static_cast<uint32_t>(send_buffer.size())
    };
    std::array<uint8_t, 5> serialized_header = protocol::SerializeHeader(header);

    if (send(client_socket, serialized_header.data(), serialized_header.size(), 0) != serialized_header.size()) {
      FatalError("PULL: send() failed for message header");
    }

    // Send file contents
    int bytes_sent;
    unsigned int total_bytes_sent = 0;
    uint32_t total_size = send_buffer.size();

    while (total_bytes_sent < total_size) {
      bytes_sent = send(client_socket, send_buffer.data() + total_bytes_sent, total_size - total_bytes_sent, 0);

      if (bytes_sent < 0) {
        FatalError("send() failed for PULL response file");
      } else if (bytes_sent == 0) {
        FatalError("Client disconnected while sending PULL response.");
      }

      total_bytes_sent += bytes_sent;
    }
  }

  std::cout << "PULL operation completed." << "\n";
}

void HandleLeave(int client_socket) {
  if (close(client_socket) < 0) {
    FatalError("close() failed");
  }
  std::cout << "Client connection closed." << "\n";
}