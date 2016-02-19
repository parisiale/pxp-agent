#include <pxp-agent/action_response.hpp>

#include <cpp-pcp-client/validator/validator.hpp>
#include <cpp-pcp-client/validator/schema.hpp>

#include <leatherman/util/time.hpp>

#define LEATHERMAN_LOGGING_NAMESPACE "puppetlabs.pxp_agent.action_response"
#include <leatherman/logging/logging.hpp>

#include <boost/format.hpp>

#include <cassert>
#include <utility>  // std::forward

namespace PXPAgent {

namespace lth_jc = leatherman::json_container;
namespace lth_util = leatherman::util;
using R_T = ActionResponse::ResponseType;

const std::string ACTION_METADATA_SCHEMA { "action_metdata_schema" };

const std::string REQUESTER { "requester" };
const std::string MODULE { "module" };
const std::string ACTION { "action" };
const std::string REQUEST_PARAMS { "request_params" };
const std::string TRANSACTION_ID { "transaction_id" };
const std::string REQUEST_ID { "request_id" };
const std::string NOTIFY_OUTCOME { "notify_outcome" };
const std::string START { "start" };
const std::string STATUS { "status" };
const std::string END { "end" };
const std::string RESULTS { "results" };
const std::string RESULTS_ARE_VALID { "results_are_valid" };
const std::string EXECUTION_ERROR { "execution_error" };

static PCPClient::Validator getActionMetadataValidator()
{
    using T_C = PCPClient::TypeConstraint;
    PCPClient::Schema sch { ACTION_METADATA_SCHEMA, PCPClient::ContentType::Json };

    // Entries created during initialization (all mandatory)
    sch.addConstraint(REQUESTER, T_C::String, true);
    sch.addConstraint(MODULE, T_C::String, true);
    sch.addConstraint(ACTION, T_C::String, true);
    sch.addConstraint(REQUEST_PARAMS, T_C::String, true);
    sch.addConstraint(TRANSACTION_ID, T_C::String, true);
    sch.addConstraint(REQUEST_ID, T_C::String, true);
    sch.addConstraint(NOTIFY_OUTCOME, T_C::Bool, true);
    sch.addConstraint(START, T_C::String, true);
    sch.addConstraint(STATUS, T_C::String, true);

    // Entries created after processing the action's output
    sch.addConstraint(END, T_C::String, false);
    sch.addConstraint(RESULTS, T_C::Any, false);
    sch.addConstraint(RESULTS_ARE_VALID, T_C::Bool, false);
    sch.addConstraint(EXECUTION_ERROR, T_C::String, false);

    PCPClient::Validator validator {};
    validator.registerSchema(sch);
    return validator;
}

//
// Static member functions
//

lth_jc::JsonContainer
ActionResponse::getMetadataFromRequest(const ActionRequest& request)
{
    lth_jc::JsonContainer m {};
    m.set<std::string>(REQUESTER, request.sender());
    m.set<std::string>(MODULE, request.module());
    m.set<std::string>(ACTION, request.action());
    m.set<std::string>(REQUEST_PARAMS,
        request.paramsTxt().empty() ? "none" : request.paramsTxt());

    m.set<std::string>(TRANSACTION_ID, request.transactionId());
    m.set<std::string>(REQUEST_ID, request.id());
    m.set<bool>(NOTIFY_OUTCOME, request.notifyOutcome());
    m.set<std::string>(START, lth_util::get_ISO8601_time());
    m.set<std::string>(STATUS, ACTION_STATUS_NAMES.at(ActionStatus::Running));

    return m;
}

//
// Public interface
//

ActionResponse::ActionResponse(ModuleType module_type_,
                               const ActionRequest& request)
        : module_type { module_type_ },
          request_type { request.type() },
          output {},
          action_metadata { getMetadataFromRequest(request) }
{
}

ActionResponse::ActionResponse(ModuleType module_type_,
                               RequestType request_type_,
                               ActionOutput output_,
                               lth_jc::JsonContainer&& action_metadata_)
        : module_type { module_type_ },
          request_type { request_type_ },
          output { output_ },
          action_metadata { action_metadata_ }
{
    if (!valid())
        throw Error { "invalid action metadata" };
}

void ActionResponse::setStatus(ActionStatus status)
{
    action_metadata.set<std::string>(STATUS, ACTION_STATUS_NAMES.at(status));
}

void ActionResponse::setValidResultsAndEnd(lth_jc::JsonContainer&& results,
                                           const std::string& execution_error)
{
    action_metadata.set<std::string>(END, lth_util::get_ISO8601_time());
    action_metadata.set<bool>(RESULTS_ARE_VALID, true);
    action_metadata.set<lth_jc::JsonContainer>(RESULTS,
        std::forward<lth_jc::JsonContainer>(results));
    action_metadata.set<std::string>(STATUS,
        ACTION_STATUS_NAMES.at(ActionStatus::Success));
    if (!execution_error.empty())
        action_metadata.set<std::string>(EXECUTION_ERROR, execution_error);
}

void ActionResponse::setBadResultsAndEnd(const std::string& execution_error)
{
    action_metadata.set<std::string>(END, lth_util::get_ISO8601_time());
    action_metadata.set<bool>(RESULTS_ARE_VALID, false);
    action_metadata.set<std::string>(EXECUTION_ERROR, execution_error);
    action_metadata.set<std::string>(STATUS,
        ACTION_STATUS_NAMES.at(ActionStatus::Failure));
}

const std::string& ActionResponse::prettyRequestLabel() const
{
    if (pretty_request_label_.empty())
        pretty_request_label_ =
            (boost::format("%1% '%2% %3%' request (transaction %4%)")
                % REQUEST_TYPE_NAMES.at(request_type)
                % action_metadata.get<std::string>(MODULE)
                % action_metadata.get<std::string>(ACTION)
                % action_metadata.get<std::string>(TRANSACTION_ID))
            .str();

    return pretty_request_label_;
}

bool ActionResponse::isValidActionMetadata(const lth_jc::JsonContainer& metadata)
{
    static PCPClient::Validator validator { getActionMetadataValidator() };
    try {
        validator.validate(metadata, ACTION_METADATA_SCHEMA);
        return true;
    } catch (const PCPClient::validation_error& e) {
        LOG_TRACE("Invalid action metadata: %1%", e.what());
    }
    return false;
}


bool ActionResponse::valid() const
{
    return ActionResponse::isValidActionMetadata(action_metadata);
}

bool ActionResponse::valid(R_T response_type) const
{
    if (!valid())
        return false;

    switch (response_type) {
        case (R_T::Blocking):
        case (R_T::NonBlocking):
            return action_metadata.includes(RESULTS);
        case (R_T::StatusOutput):
            return true;
        case (R_T::RPCError):
            return action_metadata.includes(EXECUTION_ERROR);
    }
}

// TODO(ale): update this after PXP v2.0 changes
lth_jc::JsonContainer ActionResponse::toJSON(R_T response_type) const
{
    lth_jc::JsonContainer r {};
    r.set<std::string>(TRANSACTION_ID,
        action_metadata.get<std::string>(TRANSACTION_ID));

    switch (response_type) {
        case (R_T::Blocking):
        case (R_T::NonBlocking):
            r.set<lth_jc::JsonContainer>(RESULTS,
                action_metadata.get<lth_jc::JsonContainer>(RESULTS));
            break;
        case (R_T::StatusOutput):
            r.set<std::string>(STATUS, action_metadata.get<std::string>(STATUS));
            r.set<std::string>("stdout", output.stdout);
            r.set<std::string>("stderr", output.stderr);
            r.set<int>("exitcode", output.exitcode);
            break;
        case (R_T::RPCError):
            r.set<std::string>("id", action_metadata.get<std::string>(REQUEST_ID));
            r.set<std::string>("description",
                action_metadata.get<std::string>(EXECUTION_ERROR));
            break;
    }

    return r;
}

}  // namespace PXPAgent