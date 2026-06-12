#include "dsa/soc/alert_handler.h"
#include "dsa/soc/alert_handler_factory.h"

#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

namespace dsa::soc {

// Appends one JSON object per line (ndjson). Thread-safe via an internal mutex
// so multiple producer threads (future) can share a single handler instance.
class FileAlertHandler : public AlertHandler {
public:
    explicit FileAlertHandler(const std::string& path)
        : path_(path), out_(path, std::ios::out | std::ios::trunc) {
        if (!out_.is_open()) {
            throw std::runtime_error("FileAlertHandler: cannot open " + path);
        }
    }

    void send(const Alert& alert) override {
        std::lock_guard<std::mutex> lk(m_);
        out_ << alert.to_json() << '\n';
        out_.flush();  // durability > throughput for alerts
    }

    void flush() override {
        std::lock_guard<std::mutex> lk(m_);
        out_.flush();
    }

    std::string name() const override { return "file:" + path_; }

private:
    std::string   path_;
    std::ofstream out_;
    std::mutex    m_;
};

std::unique_ptr<AlertHandler> make_file_alert_handler(const HandlerConfig& cfg) {
    std::string path = cfg.get("path", "output/alerts.json");
    return std::make_unique<FileAlertHandler>(path);
}

} // namespace dsa::soc
