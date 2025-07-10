#include <iostream>
#include <fstream>
#include <array>
#include <filesystem>
#include "utils.h"
#include "sha256.h"
#include "protocol/protocol.h"

void FatalError(const std::string& message) {
  std::cerr << "Fatal error: " << message << std::endl;
  exit(EXIT_FAILURE);
}

std::vector<protocol::FileHeader> ListFilesWithHashes(const std::filesystem::path& dir) {
  std::vector<protocol::FileHeader> out;

  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }

    auto path = entry.path();
    std::string file_name = path.filename().string();

    // Compute hash
    SHA256 sha256;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
      FatalError("Failed to open file: " + file_name);
    }

    std::array<char, 4096> buffer;
    while (in.read(buffer.data(), buffer.size()) || in.gcount() > 0) {
      sha256.add(buffer.data(), in.gcount());
    }

    protocol::FileHeader file_header;
    file_header.name_length = static_cast<uint8_t>(file_name.size());
    file_header.name = file_name;
    file_header.hash_length = protocol::kSha256HexLen;
    file_header.hash = sha256.getHash();
    out.push_back(file_header);
  }

  return out;
}