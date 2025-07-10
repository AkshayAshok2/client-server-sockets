#include <string>
#include <vector>
#include <filesystem>
#include "protocol/protocol.h"
#include "sha256.h"

void FatalError(const std::string& message);

std::vector<protocol::FileHeader> ListFilesWithHashes(const std::filesystem::path& dir);