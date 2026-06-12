#include "dsa/soc/alert_handler.h"
#include "dsa/soc/alert_handler_factory.h"

#include <iostream>
#include <memory>
#include <mutex>

namespace dsa::soc {

// Writes alerts to stdout/stderr — useful for container logs shipped to Loki.
class StdoutAlertHandler : public AlertHandler {
public:
    explicit StdoutAlertHandler(bool to_stderr) : to_stderr_(to_stderr) {}

    void send(const Alert& alert) override {
        std::lock_guard<std::mutex> lk(m_);
        auto& os = to_stderr_ ? std::cerr : std::cout;
        os << alert.to_json() << '\n';
        os.flush();
    }

    std::string name() const override {
        return to_stderr_ ? "stderr" : "stdout";
    }

private:
    bool       to_stderr_;
    std::mutex m_;
};

std::unique_ptr<AlertHandler> make_stdout_alert_handler(const HandlerConfig& cfg) {
    bool to_stderr = cfg.get("stream", "stderr") == "stderr";
    return std::make_unique<StdoutAlertHandler>(to_stderr);
}

} // namespace dsa::soc
