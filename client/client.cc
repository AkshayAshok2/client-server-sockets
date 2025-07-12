#include <cstdint>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string>
#include <cstring>
#include <iostream>
#include <filesystem>
#include <vector>
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
  int client_socket_;
  std::vector<protocol::FileHeader> client_files_;
  std::vector<protocol::FileHeader> server_files_;
  std::vector<protocol::FileHeader> diff_files_;
};

void ClientApp::HandleList() {
  protocol::MessageHeader header {
    .command = protocol::Command::LIST,
    .payload_size = 0
  };
  std::array<uint8_t, 5> serialized_header = protocol::SerializeHeader(header);
  
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

  protocol::MessageHeader received_header = protocol::DeserializeHeader(received_serialized_header);
  total_size = received_header.payload_size;
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

  // Show the list of files and store in app state
  this->server_files_.clear();
  std::cout << "Received LIST response with " << static_cast<int>(response.file_count) << " files." << "\n";
  for (const auto &file : response.files) {
    std::cout << "File: " << file.name << "\nHash: " << file.hash << "\n";
    this->server_files_.push_back(file);
  }

  std::cout << "LIST completed." << "\n";
  this->state_ = State::Listed;
}

void ClientApp::HandleDiff() {
  if (this->state_ != State::Listed || this->state_ == State::Diffed || this->state_ == State::Pulled) {
    std::cout << "You must LIST files before performing DIFF." << "\n";
    return;
  }

  // Initialize data directory
  const char *run_files = std::getenv("RUNFILES_DIR");
  std::filesystem::path data_dir = run_files
      ? std::filesystem::path(run_files) / "client_server_sockets" / "client" / "files"
      : std::filesystem::current_path() / "client" / "files";

  this->client_files_ = ListFilesWithHashes(data_dir);
  std::vector<protocol::FileHeader> client_missing_files;

  for (const auto& server_file : this->server_files_) {
    bool found = false;

    for (const auto& client_file : this->client_files_) {
      if (server_file.hash == client_file.hash) {
        found = true;
        break;
      }
    }

    if (!found) {
      client_missing_files.push_back(server_file);
    }
  }

  this->diff_files_ = client_missing_files;

  // Show the DIFF of files (for now, just files missing on the client that the server has)
  // TODO: make DIFF 2-way
  std::cout << "DIFF completed. Found " << this->diff_files_.size() << " files missing on the client." << "\n";
  for (const auto &file : this->diff_files_) {
    std::cout << "Missing File: " << file.name << "\nHash: " << file.hash << "\n";
  }

  this->state_ = State::Diffed;
}

void ClientApp::HandlePull() {
  if (this->state_ != State::Diffed || this->state_ == State::Pulled) {
    std::cout << "You must DIFF files before performing PULL." << "\n";
    return;
  }

  // Prepare PULL request
  protocol::PullRequest pull_request {
    .file_count = static_cast<uint8_t>(this->diff_files_.size()),
    .files = this->diff_files_
  };

  std::vector<uint8_t> serialized_request = protocol::SerializePullRequest(pull_request);
  uint32_t total_size = serialized_request.size();

  // Send message header to server
  protocol::MessageHeader header {
    .command = protocol::Command::PULL,
    .payload_size = total_size
  };
  std::array<uint8_t, 5> serialized_header = protocol::SerializeHeader(header);

  if (send(this->client_socket_, serialized_header.data(), serialized_header.size(), 0) != serialized_header.size()) {
    FatalError("send() failed for PULL command header");
  }

  // Send PULL request payload to server
  unsigned int total_bytes_sent = 0;
  int bytes_sent;

  while (total_bytes_sent < total_size) {
    bytes_sent = send(this->client_socket_, serialized_request.data() + total_bytes_sent, total_size - total_bytes_sent, 0);

    if (bytes_sent < 0) {
      FatalError("send() failed for PULL request payload");
    } else if (bytes_sent == 0) {
      FatalError("Server disconnected while sending PULL request.");
    }

    total_bytes_sent += bytes_sent;
  }

  // Receive PULL response from server
  unsigned int n_files = this->diff_files_.size();

  for (unsigned int i = 0; i < n_files; i++) {
    std::array<uint8_t, 5> response_header_buffer;

    // Receive response header
    if (recv(this->client_socket_, response_header_buffer.data(), response_header_buffer.size(), 0) != response_header_buffer.size()) {
      FatalError("recv() failed for PULL response header");
    }

    protocol::MessageHeader response_header = protocol::DeserializeHeader(response_header_buffer);
    uint32_t payload_size = response_header.payload_size;

    // Receive response payload
    std::vector<uint8_t> receive_buffer(payload_size);
    unsigned int total_bytes_received = 0;
    int bytes_received;
    
    while (total_bytes_received < payload_size) {
      bytes_received = recv(this->client_socket_, receive_buffer.data() + total_bytes_received, payload_size - total_bytes_received, 0);

      if (bytes_received < 0) {
        FatalError("recv() failed for PULL response file bytes");
      } else if (bytes_received == 0) {
        FatalError("Server disconnected while sending PULL response file bytes.");
      }

      total_bytes_received += bytes_received;
    }

    protocol::FileContents file = protocol::DeserializeFileContents(receive_buffer);

    // Initialize data directory
    std::filesystem::path data_dir = std::filesystem::current_path() / "client" / "files";
    WriteFileBytes(file, data_dir);

    std::cout << "Received and wrote file: " << file.header.name << "\n";
  }
}

void ClientApp::HandleLeave() {
  protocol::MessageHeader header {
    .command = protocol::Command::LEAVE,
    .payload_size = 0
  };
  std::array<uint8_t, 5> serialized_header = protocol::SerializeHeader(header);

  if (send(this->client_socket_, serialized_header.data(), serialized_header.size(), 0) != serialized_header.size()) {
    FatalError("send() failed for LEAVE command");
  }

  if (close(this->client_socket_) < 0) {
    FatalError("close() failed");
  }

  std::cout << "Client connection closed." << "\n";
  exit(EXIT_SUCCESS);
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