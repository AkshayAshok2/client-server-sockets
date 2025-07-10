#pragma once

#include <iostream>
#include <string>
#include <array>
#include <vector>
#include <sys/types.h>

namespace protocol {
  constexpr uint16_t kReceiveBufferSize = 512;
  constexpr uint16_t kSendBufferSize    = 512;
  constexpr uint8_t  kSha256HexLen      = 64;

  enum class Command : uint8_t {
    LIST = 1,
    DIFF = 2,
    PULL = 3,
    LEAVE = 4
  };

  struct FileHeader {
    uint8_t name_length;
    std::string name;
    uint8_t hash_length;
    std::string hash;
  };

  struct FileChunk {
    uint32_t chunk_size;
    std::vector<uint8_t> chunk_data;
  };

  struct FileContents {
    FileHeader header;
    uint64_t file_size;
    std::vector<FileChunk> chunks;
  };

  struct MessageHeader {
    Command command;
    uint32_t payload_size;
  };

  struct ListResponse {\
    uint8_t file_count;
    std::vector<FileHeader> files;
  };

  struct PullRequest {
    uint8_t file_count;
    std::vector<FileHeader> files;
  };

  struct PullResponse {
    uint8_t file_count;
    std::vector<FileContents> files;
  };
}