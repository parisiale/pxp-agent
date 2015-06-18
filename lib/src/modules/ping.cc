#include <cthun-agent/modules/ping.hpp>
#include <cthun-agent/errors.hpp>

#include <boost/date_time/posix_time/posix_time.hpp>

#include <ctime>
#include <string>
#include <sstream>

#define LEATHERMAN_LOGGING_NAMESPACE "puppetlabs.cthun_agent.modules.ping"
#include <leatherman/logging/logging.hpp>

namespace CthunAgent {
namespace Modules {

namespace V_C = valijson::constraints;

static const std::string PING { "ping" };

Ping::Ping() {
    module_name = PING;
    actions.push_back(PING);
    CthunClient::Schema input_schema { PING };
    input_schema.addConstraint("sender_timestamp",
                               CthunClient::TypeConstraint::String);
    CthunClient::Schema output_schema { PING };

    input_validator_.registerSchema(input_schema);
    output_validator_.registerSchema(output_schema);
}

lth_jc::JsonContainer Ping::ping(const ActionRequest& request) {
    lth_jc::JsonContainer data {};

    if (request.parsedChunks().debug.empty()) {
        LOG_ERROR("Found no debug entry in the request message");
        throw request_processing_error { "no debug entry" };
    }

    auto& debug_entry = request.parsedChunks().debug[0];

    try {
        data.set<std::vector<lth_jc::JsonContainer>>(
                "request_hops",
                debug_entry.get<std::vector<lth_jc::JsonContainer>>("hops"));
    } catch (lth_jc::data_parse_error& e) {
        LOG_ERROR("Failed to parse debug entry: %1%", e.what());
        LOG_DEBUG("Debug entry: %1%", debug_entry.toString());
        throw request_processing_error { "debug entry is not valid JSON" };
    }
    return data;
}

ActionOutcome Ping::callAction(const ActionRequest& request) {
   return ActionOutcome { ping(request) };
}

}  // namespace Modules
}  // namespace CthunAgent
