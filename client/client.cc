#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <cstring>
#include <iostream>
#include "utils/utils.h"
#include "protocol/protocol.h"
#include "protocol/serialization.h"

enum class State { Fresh, Listed, Diffed, Pulled };

class ClientApp {
 public:
  ClientApp(unsigned int client_socket) : client_socket_(client_socket) {}
  void HandleList();
  void HandleDiff();
  void HandlePull();
  void HandleLeave();

 private:
  State state_ = State::Fresh;
  unsigned int client_socket_;
  std::vector<protocol::FileHeader> client_files_;
  std::vector<protocol::FileHeader> server_files_;
  std::vector<protocol::FileContents> diff_files_;
};

void ClientApp::HandleList() {
  protocol::MessageHeader header {
    .command = protocol::Command::LIST,
    .payload_size = 0
  };
  std::array<uint8_t, 5> serialized_header = protocol::SerializeHeader(header);
  this->state_ = State::Listed;
  
  if (send(this->client_socket_, serialized_header.data(), serialized_header.size(), 0) != serialized_header.size()) {
    FatalError("send() failed for LIST command");
  }

  // Receive number of files
  std::array<uint8_t, 5> received_serialized_header;
  uint32_t total_size;
  uint32_t total_bytes_received = 0;
  uint32_t bytes_received;
  std::vector<uint8_t> receive_buffer;

  // Receive header bytes from server
  if (recv(this->client_socket_, received_serialized_header.data(), received_serialized_header.size(), 0) != received_serialized_header.size()) {
    FatalError("recv() failed for LIST header");
  }

  total_size = ntohl(*(uint32_t *)(received_serialized_header.data() + 1));
  receive_buffer.resize(total_size);

  // Receive payload bytes from server
  while (total_bytes_received < total_size) {
    bytes_received = recv(this->client_socket_, receive_buffer.data() + total_bytes_received, total_size - total_bytes_received, 0);

    if (bytes_received < 0) {
      FatalError("recv() failed for LIST payload");
    } else if (bytes_received == 0) {
      FatalError("Server disconnected while sending LIST response.");
    }

    total_bytes_received += bytes_received;
  }

  // Deserialize the received data
  protocol::ListResponse response = protocol::DeserializeList(receive_buffer);

  // Show the list of files
  std::cout << "Received LIST response with " << static_cast<int>(response.file_count) << " files." << "\n";
  for (const auto &file : response.files) {
    std::cout << "File: " << file.name << "\nHash: " << file.hash << "\n";
    this->server_files_.push_back(file);
  }

  std::cout << "LIST completed." << "\n";
}

void ClientApp::HandleDiff() {
  std::cout << "DIFF not implemented yet." << "\n";
}

void ClientApp::HandlePull() {
  std::cout << "PULL not implemented yet." << "\n";
}

void ClientApp::HandleLeave() {
  std::cout << "LEAVE not implemented yet." << "\n";
}

int main(int argc, char *argv[]) {
  unsigned int client_socket;
  sockaddr_in server_address;
  const std::string server_ip_address = "127.0.0.1";
  const unsigned int server_port = 9090;
  int option;

  // Create a new TCP socket
  if ((client_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    FatalError("socket() failed");
  }

  // Initialize the client application
  ClientApp client = ClientApp(client_socket);

  // Construct the server address structure
  server_address.sin_family      = AF_INET;
  server_address.sin_port        = htons(server_port);
  server_address.sin_addr.s_addr = inet_addr(server_ip_address.c_str());

  // Connect to the server
  if (connect(client_socket, (sockaddr *)&server_address, sizeof(server_address)) < 0) {
    FatalError("connect() failed");
  }

  std::cout << "Welcome to MyMusic!" << "\n";

  while (true) {
    std::cout << "\nSelect an option:\n1. LIST\n2. DIFF\n3. PULL\n4. LEAVE" << "\n";
    std::cin >> option;

    switch (option) {
      case 1: {
        // LIST
        client.HandleList();
        break;
      }
      case 2: {
        // DIFF
        client.HandleDiff();
        break;
      }
      case 3: {
        // PULL
        client.HandlePull();
        break;
      }
      case 4: {
        // LEAVE
        client.HandleLeave();
        break;
      }
      default:
        std::cout << "Invalid option. Please try again." << "\n";
        continue;
    }
  }
  return 0;
}