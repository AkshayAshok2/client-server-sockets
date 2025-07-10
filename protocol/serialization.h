#include <vector>
#include "protocol.h"
#include "utils/utils.h"

namespace protocol {
    std::array<uint8_t, 5> SerializeHeader(const MessageHeader& header);
    std::vector<uint8_t> SerializeList(const ListResponse& response);
    ListResponse DeserializeList(const std::vector<uint8_t>& in);
}