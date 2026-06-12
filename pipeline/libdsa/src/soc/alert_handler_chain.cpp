#include "dsa/soc/alert_handler.h"

#include <exception>
#include <iostream>

namespace dsa::soc {

void AlertHandlerChain::send(const Alert& alert) {
    for (auto& h : handlers_) {
        try {
            h->send(alert);
        } catch (const std::exception& e) {
            std::cerr << "[soc] handler '" << h->name()
                      << "' failed: " << e.what() << "\n";
        }
    }
}

void AlertHandlerChain::flush() {
    for (auto& h : handlers_) {
        try { h->flush(); } catch (...) {}
    }
}

} // namespace dsa::soc
