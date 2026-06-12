#pragma once
#include "dsa/soc/alert.h"

#include <memory>
#include <string>
#include <vector>

namespace dsa::soc {

// Sink abstraction for alerts. Implementations: file, stdout, nats, ...
class AlertHandler {
public:
    virtual ~AlertHandler() = default;

    // Deliver a single alert. Must not block the pipeline hot path for long;
    // network-backed handlers should be async/buffered internally.
    virtual void send(const Alert& alert) = 0;

    // Called on pipeline shutdown — flush buffers, close connections.
    virtual void flush() {}

    // Human-readable handler name (for logs/metrics).
    virtual std::string name() const = 0;
};

// Chain of handlers: fan-out to every registered sink.
// A failure in one handler does not prevent others from receiving the alert.
class AlertHandlerChain : public AlertHandler {
public:
    void add(std::unique_ptr<AlertHandler> h) { handlers_.push_back(std::move(h)); }

    void send(const Alert& alert) override;
    void flush() override;
    std::string name() const override { return "chain"; }

    size_t size() const { return handlers_.size(); }

private:
    std::vector<std::unique_ptr<AlertHandler>> handlers_;
};

} // namespace dsa::soc
