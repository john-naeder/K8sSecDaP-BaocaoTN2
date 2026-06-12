#include "dsa/soc/alert.h"

#include <sstream>

namespace dsa::soc {

static std::string escape_json(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string Alert::to_json() const {
    std::ostringstream os;
    os << "{\"type\":\""       << escape_json(type)      << "\""
       << ",\"source\":\""     << escape_json(source)    << "\""
       << ",\"timestamp_ns\":" << timestamp_ns
       << ",\"severity\":\""   << to_string(severity)    << "\""
       << ",\"status\":\""     << to_string(status)      << "\"";

    if (!incident_id.empty())
        os << ",\"incident_id\":\"" << escape_json(incident_id) << "\"";
    if (!node_name.empty())
        os << ",\"node_name\":\""   << escape_json(node_name)   << "\"";
    if (!namespace_.empty())
        os << ",\"namespace\":\""   << escape_json(namespace_)  << "\"";

    if (!tags.empty()) {
        os << ",\"tags\":[";
        for (size_t i = 0; i < tags.size(); ++i) {
            if (i) os << ',';
            os << '"' << escape_json(tags[i]) << '"';
        }
        os << "]";
    }

    // Details is already raw JSON (object) produced by the pipeline.
    os << ",\"details\":" << (details.empty() ? "{}" : details) << "}";
    return os.str();
}

} // namespace dsa::soc
