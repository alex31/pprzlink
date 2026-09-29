// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <cstdint>

namespace pprzlink {
  struct TransportStatistics {
    uint64_t receivedMessages = 0;
    uint64_t receivedMessageBytes = 0; // Valid frames, including the transport envelope.
    uint64_t checksumErrors = 0;
    uint64_t lengthErrors = 0;
    uint64_t decodingErrors = 0;
    uint64_t discardedBytes = 0;
  };
}
