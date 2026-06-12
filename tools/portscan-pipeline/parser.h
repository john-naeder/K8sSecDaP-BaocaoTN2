// Parse một dòng JSON Lines (ưu tiên) hoặc dòng tcpdump text (phụ)
// thành NetworkEvent. Cô đọng từ pipeline/src/engine.cpp:87-150 (stage-2).
#pragma once
#include "event.h"
#include <string>

namespace portscan {

// Trả về true nếu parse thành công và đã ghi vào evt.
// Hỗ trợ hai định dạng:
//   1. JSON Lines:  {"src_ip":"10.0.5.99","dst_ip":"10.0.0.42","dst_port":80,"timestamp_ns":...}
//   2. tcpdump:     21:30:42.123 IP 10.0.5.99.34522 > 10.0.0.42.80: Flags [S], ...
bool parse_event(const std::string& line, NetworkEvent& evt);

} // namespace portscan
