#pragma once
#include "dsa/soc/alert_handler.h"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace dsa::soc {

// Config passed to every handler factory. Keep it flat — handlers pick the
// keys they care about. Values are strings; handlers parse as needed.
struct HandlerConfig {
    std::string name;   // handler type: "file", "stdout", "nats"
    std::unordered_map<std::string, std::string> params;

    std::string get(const std::string& key, const std::string& fallback = "") const {
        auto it = params.find(key);
        return it == params.end() ? fallback : it->second;
    }
};

// Registry specific to AlertHandlers — factories take a HandlerConfig since
// each instance needs its own configuration (path, url, subject, ...).
class AlertHandlerRegistry {
public:
    using FactoryFn = std::function<std::unique_ptr<AlertHandler>(const HandlerConfig&)>;

    static AlertHandlerRegistry& instance() {
        static AlertHandlerRegistry r;
        return r;
    }

    void register_handler(const std::string& name, FactoryFn factory) {
        factories_[name] = std::move(factory);
    }

    std::unique_ptr<AlertHandler> create(const HandlerConfig& cfg) const {
        auto it = factories_.find(cfg.name);
        if (it == factories_.end()) {
            throw std::runtime_error("Unknown alert handler: " + cfg.name);
        }
        return it->second(cfg);
    }

    bool has(const std::string& name) const {
        return factories_.count(name) > 0;
    }

    std::vector<std::string> available() const {
        std::vector<std::string> names;
        names.reserve(factories_.size());
        for (const auto& [k, _] : factories_) names.push_back(k);
        return names;
    }

private:
    AlertHandlerRegistry() = default;
    std::unordered_map<std::string, FactoryFn> factories_;
};

// Register all compiled-in handlers. NATS handler is only registered when
// built with BUILD_SOC_NATS=ON.
void register_all_alert_handlers();

} // namespace dsa::soc
