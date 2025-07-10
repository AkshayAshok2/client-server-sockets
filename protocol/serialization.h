#include <vector>
#include "protocol.h"
#include "utils/utils.h"

namespace protocol {
    std::array<uint8_t, 5> SerializeHeader(const MessageHeader& header);
    MessageHeader DeserializeHeader(const std::array<uint8_t, 5>& in);
    std::vector<uint8_t> SerializeList(const ListResponse& response);
    ListResponse DeserializeList(const std::vector<uint8_t>& in);
    std::vector<uint8_t> SerializePullRequest(const PullRequest& request);
    PullRequest DeserializePullRequest(const std::vector<uint8_t>& in);
    std::vector<uint8_t> SerializePullResponse(const PullResponse& response);
    PullResponse DeserializePullResponse(const std::vector<uint8_t>& in);
}