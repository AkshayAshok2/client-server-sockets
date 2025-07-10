#include "serialization.h"
#include "protocol.h"

namespace protocol {
  std::array<uint8_t, 5> SerializeHeader(const MessageHeader& header) {
    std::array<uint8_t, 5> buffer;
    buffer[0] = static_cast<uint8_t>(header.command);
    uint32_t payload_size = htonl(header.payload_size);
    std::memcpy(buffer.data() + 1, &payload_size, sizeof(payload_size));
    return buffer;
  }

  MessageHeader DeserializeHeader(const std::array<uint8_t, 5>& in) {
    if (in.size() != 5) {
      FatalError("Invalid input size for MessageHeader deserialization");
    }

    MessageHeader header;
    header.command = static_cast<Command>(in[0]);
    uint32_t payload_size;
    std::memcpy(&payload_size, in.data() + 1, sizeof(payload_size));
    header.payload_size = ntohl(payload_size);
    
    return header;
  }

  std::vector<uint8_t> SerializeList(const ListResponse& response) {
    std::vector<uint8_t> out;
    out.push_back(response.file_count);

    for (const auto& file : response.files) {
      out.push_back(file.name_length);
      out.insert(out.end(), file.name.begin(), file.name.end());
      out.push_back(file.hash_length);
      out.insert(out.end(), file.hash.begin(), file.hash.end());
    }

    return out;
  }

  ListResponse DeserializeList(const std::vector<uint8_t>& in) {
    ListResponse response;
    
    if (in.empty()) {
      FatalError("Empty input for ListResponse deserialization");
    }

    size_t offset = 0;
    uint8_t current_file_count = 0;
    response.file_count = in[offset++];
    response.files = std::vector<FileHeader>();
    
    while (offset < in.size()) {
        uint8_t file_name_length = in[offset++];

        if (offset + file_name_length > in.size()) {
          FatalError("Invalid input for ListResponse deserialization: file name length exceeds input size");
        }

        std::string file_name(in.begin() + offset, in.begin() + offset + file_name_length);
        offset += file_name_length;

        if (offset >= in.size()) {
          FatalError("Invalid input for ListResponse deserialization: not enough data for file hash");
        }

        uint8_t file_hash_length = in[offset++];

        if (offset + file_hash_length > in.size()) {
          FatalError("Invalid input for ListResponse deserialization: file hash length exceeds input size");
        }

        std::string file_hash(in.begin() + offset, in.begin() + offset + file_hash_length);
        offset += file_hash_length;

        FileHeader file_header{
            .name_length = file_name_length,
            .name = file_name,
            .hash_length = file_hash_length,
            .hash = file_hash
        };

        response.files.push_back(file_header);
        current_file_count++;
    }

    if (current_file_count != response.file_count) {
      FatalError("File count mismatch in ListResponse deserialization:"
                 " expected " + std::to_string(response.file_count) +
                 ", got " + std::to_string(current_file_count));
    }

    return response;
  }

  std::vector<uint8_t> SerializePullRequest(const PullRequest& request) {
    std::vector<uint8_t> out;
    out.push_back(request.file_count);

    for (const auto& file : request.files) {
      out.push_back(file.name_length);
      out.insert(out.end(), file.name.begin(), file.name.end());
      out.push_back(file.hash_length);
      out.insert(out.end(), file.hash.begin(), file.hash.end());
    }

    return out;
  }

  PullRequest DeserializePullRequest(const std::vector<uint8_t> &in) {
    PullRequest request;
    
    if (in.empty()) {
      FatalError("Empty input for PullRequest deserialization");
    }

    size_t offset = 0;
    uint8_t current_file_count = 0;
    request.file_count = in[offset++];
    request.files = std::vector<FileHeader>();
    
    while (offset < in.size()) {
        uint8_t file_name_length = in[offset++];

        if (offset + file_name_length > in.size()) {
          FatalError("Invalid input for PullRequest deserialization: file name length exceeds input size");
        }

        std::string file_name(in.begin() + offset, in.begin() + offset + file_name_length);
        offset += file_name_length;

        if (offset >= in.size()) {
          FatalError("Invalid input for PullRequest deserialization: not enough data for file hash");
        }

        uint8_t file_hash_length = in[offset++];

        if (offset + file_hash_length > in.size()) {
          FatalError("Invalid input for PullRequest deserialization: file hash length exceeds input size");
        }

        std::string file_hash(in.begin() + offset, in.begin() + offset + file_hash_length);
        offset += file_hash_length;

        FileHeader file_header{
            .name_length = file_name_length,
            .name = file_name,
            .hash_length = file_hash_length,
            .hash = file_hash
        };

        request.files.push_back(file_header);
        current_file_count++;
    }

    if (current_file_count != request.file_count) {
      FatalError("File count mismatch in ListResponse deserialization:"
                 " expected " + std::to_string(request.file_count) +
                 ", got " + std::to_string(current_file_count));
    }

    return request;
  }

  std::vector<uint8_t> SerializePullResponse(const PullResponse& response) {
    std::vector<uint8_t> out;
    out.push_back(response.file_count);

    for (const auto& file : response.files) {
      out.push_back(file.header.name_length);
      out.insert(out.end(), file.header.name.begin(), file.header.name.end());
      out.push_back(file.header.hash_length);
      out.insert(out.end(), file.header.hash.begin(), file.header.hash.end());
      uint64_t file_size = htonll(file.file_size);
      out.insert(out.end(), reinterpret_cast<uint8_t*>(&file_size), reinterpret_cast<uint8_t*>(&file_size) + sizeof(file_size));
      
      for (const auto& chunk : file.chunks) {
        out.push_back(static_cast<uint8_t>(chunk.chunk_size >> 24));
        out.push_back(static_cast<uint8_t>(chunk.chunk_size >> 16));
        out.push_back(static_cast<uint8_t>(chunk.chunk_size >> 8));
        out.push_back(static_cast<uint8_t>(chunk.chunk_size));
        out.insert(out.end(), chunk.chunk_data.begin(), chunk.chunk_data.end());
      }
    }

    return out;
  }
}