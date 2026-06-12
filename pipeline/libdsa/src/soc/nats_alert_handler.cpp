#include "dsa/soc/alert_handler.h"
#include "dsa/soc/alert_handler_factory.h"

#include <iostream>
#include <memory>
#include <mutex>
#include <string>

// When BUILD_SOC_NATS=ON, compile against the real nats.h client.
// Otherwise, we still register a handler so configs referencing "nats" load,
// but alerts are written to stderr with a "[nats-stub]" prefix so the operator
// can see the payload that *would* have been published.
#ifdef DSA_HAVE_NATS
#include <nats/nats.h>
#endif

namespace dsa::soc {

#ifdef DSA_HAVE_NATS

class NatsAlertHandler : public AlertHandler {
public:
    NatsAlertHandler(const std::string& url, const std::string& subject)
        : url_(url), subject_(subject) {
        natsStatus s = natsConnection_ConnectTo(&conn_, url_.c_str());
        if (s != NATS_OK) {
            throw std::runtime_error(std::string("NATS connect failed: ") + natsStatus_GetText(s));
        }
    }

    ~NatsAlertHandler() override {
        if (conn_) {
            natsConnection_Flush(conn_);
            natsConnection_Destroy(conn_);
        }
    }

    void send(const Alert& alert) override {
        std::lock_guard<std::mutex> lk(m_);
        std::string payload = alert.to_json();
        natsStatus s = natsConnection_Publish(conn_, subject_.c_str(),
                                              payload.data(), (int)payload.size());
        if (s != NATS_OK) {
            std::cerr << "[nats] publish failed on " << subject_
                      << ": " << natsStatus_GetText(s) << "\n";
        }
    }

    void flush() override {
        if (conn_) natsConnection_Flush(conn_);
    }

    std::string name() const override { return "nats:" + subject_; }

private:
    std::string     url_;
    std::string     subject_;
    natsConnection* conn_ = nullptr;
    std::mutex      m_;
};

#else // !DSA_HAVE_NATS — stub

class NatsAlertHandler : public AlertHandler {
public:
    NatsAlertHandler(const std::string& url, const std::string& subject)
        : url_(url), subject_(subject) {
        std::cerr << "[nats-stub] built without nats-c; alerts for "
                  << subject_ << " will log to stderr only\n";
    }

    void send(const Alert& alert) override {
        std::lock_guard<std::mutex> lk(m_);
        std::cerr << "[nats-stub " << subject_ << "] " << alert.to_json() << "\n";
    }

    std::string name() const override { return "nats-stub:" + subject_; }

private:
    std::string url_;
    std::string subject_;
    std::mutex  m_;
};

#endif

std::unique_ptr<AlertHandler> make_nats_alert_handler(const HandlerConfig& cfg) {
    std::string url     = cfg.get("url",     "nats://nats.soc.svc.cluster.local:4222");
    std::string subject = cfg.get("subject", "zt.alerts.raw");
    return std::make_unique<NatsAlertHandler>(url, subject);
}

} // namespace dsa::soc
