#pragma once
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace dsa {

// Generic factory registry — maps string names to factory functions.
// Usage: Registry<Interface>::instance().create("algorithm_name")
template <typename Interface>
class Registry {
public:
    using FactoryFn = std::function<std::unique_ptr<Interface>()>;

    static Registry& instance() {
        static Registry r;
        return r;
    }

    void register_algorithm(const std::string& name, FactoryFn factory) {
        factories_[name] = std::move(factory);
    }

    std::unique_ptr<Interface> create(const std::string& name) const {
        auto it = factories_.find(name);
        if (it == factories_.end()) {
            throw std::runtime_error("Unknown algorithm: " + name);
        }
        return it->second();
    }

    std::vector<std::string> available() const {
        std::vector<std::string> names;
        names.reserve(factories_.size());
        for (const auto& [k, _] : factories_) {
            names.push_back(k);
        }
        return names;
    }

    bool has(const std::string& name) const {
        return factories_.count(name) > 0;
    }

private:
    Registry() = default;
    std::unordered_map<std::string, FactoryFn> factories_;
};

} // namespace dsa
