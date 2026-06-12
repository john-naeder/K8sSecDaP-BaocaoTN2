#include "dsa/soc/alert.h"
#include "dsa/soc/alert_handler.h"
#include "dsa/soc/alert_handler_factory.h"

#include <cstdio>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

using dsa::soc::Alert;
using dsa::soc::AlertHandler;
using dsa::soc::AlertHandlerChain;
using dsa::soc::AlertHandlerRegistry;
using dsa::soc::HandlerConfig;
using dsa::soc::Severity;
using dsa::soc::Status;

namespace {

struct CaptureHandler : public AlertHandler {
    std::vector<Alert> received;
    bool fail = false;

    void send(const Alert& a) override {
        if (fail) throw std::runtime_error("boom");
        received.push_back(a);
    }
    std::string name() const override { return "capture"; }
};

Alert sample_alert() {
    Alert a;
    a.type         = "port_scan";
    a.source       = "10.244.1.5";
    a.details      = R"({"freq":42,"threshold":40})";
    a.timestamp_ns = 1'700'000'000'000ULL;
    a.severity     = Severity::High;
    a.status       = Status::New;
    a.node_name    = "worker-1";
    a.namespace_   = "app";
    a.tags         = {"scan", "reconnaissance"};
    return a;
}

} // namespace

TEST(SocAlertToJson, IncludesAllFields) {
    auto j = sample_alert().to_json();
    EXPECT_NE(j.find("\"type\":\"port_scan\""), std::string::npos);
    EXPECT_NE(j.find("\"source\":\"10.244.1.5\""), std::string::npos);
    EXPECT_NE(j.find("\"severity\":\"high\""), std::string::npos);
    EXPECT_NE(j.find("\"status\":\"new\""), std::string::npos);
    EXPECT_NE(j.find("\"node_name\":\"worker-1\""), std::string::npos);
    EXPECT_NE(j.find("\"namespace\":\"app\""), std::string::npos);
    EXPECT_NE(j.find("\"tags\":[\"scan\",\"reconnaissance\"]"), std::string::npos);
    // Details must be embedded as raw JSON, not a quoted string
    EXPECT_NE(j.find("\"details\":{\"freq\":42"), std::string::npos);
}

TEST(SocAlertToJson, EscapesControlChars) {
    Alert a = sample_alert();
    a.source = "bad\"quote\nnewline";
    auto j = a.to_json();
    EXPECT_NE(j.find("bad\\\"quote\\nnewline"), std::string::npos);
}

TEST(SocAlertToJson, EmptyDetailsBecomesEmptyObject) {
    Alert a = sample_alert();
    a.details.clear();
    auto j = a.to_json();
    EXPECT_NE(j.find("\"details\":{}"), std::string::npos);
}

TEST(SocHandlerChain, FansOutToAll) {
    AlertHandlerChain chain;
    auto h1 = std::make_unique<CaptureHandler>();
    auto h2 = std::make_unique<CaptureHandler>();
    auto* p1 = h1.get();
    auto* p2 = h2.get();
    chain.add(std::move(h1));
    chain.add(std::move(h2));

    chain.send(sample_alert());
    chain.send(sample_alert());

    EXPECT_EQ(p1->received.size(), 2u);
    EXPECT_EQ(p2->received.size(), 2u);
}

TEST(SocHandlerChain, OneFailureDoesNotPoisonOthers) {
    AlertHandlerChain chain;
    auto bad  = std::make_unique<CaptureHandler>();
    auto good = std::make_unique<CaptureHandler>();
    bad->fail = true;
    auto* good_ptr = good.get();
    chain.add(std::move(bad));
    chain.add(std::move(good));

    // Must not throw — chain absorbs handler exceptions.
    chain.send(sample_alert());
    EXPECT_EQ(good_ptr->received.size(), 1u);
}

TEST(SocRegistry, FileHandlerWritesNdjson) {
    dsa::soc::register_all_alert_handlers();

    char tmpl[] = "/tmp/soc-alerts-XXXXXX";
    int fd = mkstemp(tmpl);
    ASSERT_GE(fd, 0);
    close(fd);
    std::string path = tmpl;

    {
        HandlerConfig cfg{"file", {{"path", path}}};
        auto h = AlertHandlerRegistry::instance().create(cfg);
        h->send(sample_alert());
        h->send(sample_alert());
        h->flush();
    }

    std::ifstream in(path);
    std::string line;
    int lines = 0;
    while (std::getline(in, line)) {
        ++lines;
        EXPECT_EQ(line.front(), '{');
        EXPECT_EQ(line.back(),  '}');
    }
    EXPECT_EQ(lines, 2);
    std::remove(path.c_str());
}

TEST(SocRegistry, UnknownHandlerThrows) {
    dsa::soc::register_all_alert_handlers();
    HandlerConfig cfg{"does-not-exist", {}};
    EXPECT_THROW(AlertHandlerRegistry::instance().create(cfg), std::runtime_error);
}

TEST(SocRegistry, StdoutHandlerConstructs) {
    dsa::soc::register_all_alert_handlers();
    HandlerConfig cfg{"stderr", {}};
    auto h = AlertHandlerRegistry::instance().create(cfg);
    ASSERT_NE(h, nullptr);
    EXPECT_EQ(h->name(), "stderr");
}

TEST(SocRegistry, NatsHandlerRegistered) {
    dsa::soc::register_all_alert_handlers();
    EXPECT_TRUE(AlertHandlerRegistry::instance().has("nats"));
    // Even without libnats linked, the stub should construct without error
    // (it only logs a warning). Real publishing is only attempted on send().
    HandlerConfig cfg{"nats", {{"subject", "zt.test"}}};
    auto h = AlertHandlerRegistry::instance().create(cfg);
    ASSERT_NE(h, nullptr);
}
