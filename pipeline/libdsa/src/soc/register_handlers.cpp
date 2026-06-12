#include "dsa/soc/alert_handler_factory.h"

namespace dsa::soc {

// Forward declarations of factory functions defined in each handler TU.
std::unique_ptr<AlertHandler> make_file_alert_handler(const HandlerConfig&);
std::unique_ptr<AlertHandler> make_stdout_alert_handler(const HandlerConfig&);
std::unique_ptr<AlertHandler> make_nats_alert_handler(const HandlerConfig&);

void register_all_alert_handlers() {
    auto& reg = AlertHandlerRegistry::instance();
    reg.register_handler("file",   make_file_alert_handler);
    reg.register_handler("stdout", make_stdout_alert_handler);
    reg.register_handler("stderr", make_stdout_alert_handler);
    reg.register_handler("nats",   make_nats_alert_handler);
}

} // namespace dsa::soc
