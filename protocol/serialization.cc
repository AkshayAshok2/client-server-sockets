#include "serialization.h"

namespace protocol {
  std::array<uint8_t, 5> SerializeHeader(const MessageHeader& header) {
    std::array<uint8_t, 5> buffer;
    buffer[0] = static_cast<uint8_t>(header.command);
    uint32_t payload_size = htonl(header.payload_size);
    std::memcpy(buffer.data() + 1, &payload_size, sizeof(payload_size));
    return buffer;
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
}