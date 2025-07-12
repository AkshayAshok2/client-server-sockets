#include <string>
#include <vector>
#include <filesystem>
#include "protocol/protocol.h"
#include "sha256.h"

void FatalError(const std::string& message);

std::vector<protocol::FileHeader> ListFilesWithHashes(const std::filesystem::path& dir);

std::vector<uint8_t> ReadFileBytes(const std::filesystem::path& file_path);

void WriteFileBytes(const protocol::FileContents& file, const std::filesystem::path& data_dir);