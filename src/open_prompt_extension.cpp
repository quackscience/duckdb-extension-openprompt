#define DUCKDB_EXTENSION_MAIN
#include "open_prompt_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension_util.hpp"
#include "duckdb/common/atomic.hpp"
#include "duckdb/common/exception/http_exception.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.hpp"

#include <string>
#include <sstream>
#include <mutex>
#include <iostream>
#include "yyjson.hpp"

namespace duckdb {

static std::pair<duckdb_httplib_openssl::Client, std::string> SetupHttpClient(const std::string &url) {
    std::string scheme, domain, path;
    size_t pos = url.find("://");
    std::string mod_url = url;
    if (pos != std::string::npos) {
        scheme = mod_url.substr(0, pos);
        mod_url.erase(0, pos + 3);
    }

    pos = mod_url.find("/");
    if (pos != std::string::npos) {
        domain = mod_url.substr(0, pos);
        path = mod_url.substr(pos);
    } else {
        domain = mod_url;
        path = "/";
    }

    duckdb_httplib_openssl::Client client(domain.c_str());
    client.set_read_timeout(10, 0);  // 10 seconds
    client.set_follow_location(true); // Follow redirects

    return std::make_pair(std::move(client), path);
}

static void HandleHttpError(const duckdb_httplib_openssl::Result &res, const std::string &request_type) {
    std::string err_message = "HTTP " + request_type + " request failed. ";

    switch (res.error()) {
        case duckdb_httplib_openssl::Error::Connection:
            err_message += "Connection error.";
            break;
        case duckdb_httplib_openssl::Error::BindIPAddress:
            err_message += "Failed to bind IP address.";
            break;
        case duckdb_httplib_openssl::Error::Read:
            err_message += "Error reading response.";
            break;
        case duckdb_httplib_openssl::Error::Write:
            err_message += "Error writing request.";
            break;
        case duckdb_httplib_openssl::Error::ExceedRedirectCount:
            err_message += "Too many redirects.";
            break;
        case duckdb_httplib_openssl::Error::Canceled:
            err_message += "Request was canceled.";
            break;
        case duckdb_httplib_openssl::Error::SSLConnection:
            err_message += "SSL connection failed.";
            break;
        case duckdb_httplib_openssl::Error::SSLLoadingCerts:
            err_message += "Failed to load SSL certificates.";
            break;
        case duckdb_httplib_openssl::Error::SSLServerVerification:
            err_message += "SSL server verification failed.";
            break;
        case duckdb_httplib_openssl::Error::UnsupportedMultipartBoundaryChars:
            err_message += "Unsupported characters in multipart boundary.";
            break;
        case duckdb_httplib_openssl::Error::Compression:
            err_message += "Error during compression.";
            break;
        default:
            err_message += "Unknown error.";
            break;
    }
    throw std::runtime_error(err_message);
}

// Settings management
static std::string GetConfigValue(ClientContext &context, const string &var_name, const string &default_value) {
    Value value;
    auto &config = ClientConfig::GetConfig(context);
    if (!config.GetUserVariable(var_name, value) || value.IsNull()) {
        return default_value;
    }
    return value.ToString();
}

static void SetConfigValue(DataChunk &args, ExpressionState &state, Vector &result, 
                          const string &var_name, const string &value_type) {
    UnaryExecutor::Execute<string_t, string_t>(args.data[0], result, args.size(),
        [&](string_t value) {
            try {
                if (value == "" || value.GetSize() == 0) {
                    throw std::invalid_argument(value_type + " cannot be empty.");
                }
                
                ClientConfig::GetConfig(state.GetContext()).SetUserVariable(
                    var_name,
                    Value::CreateValue(value.GetString())
                );
                return StringVector::AddString(result, value_type + " set to: " + value.GetString());
            } catch (std::exception &e) {
                return StringVector::AddString(result, "Failed to set " + value_type + ": " + e.what());
            }
        });
}

static void SetApiToken(DataChunk &args, ExpressionState &state, Vector &result) {
    SetConfigValue(args, state, result, "openprompt_api_token", "API token");
}

static void SetApiUrl(DataChunk &args, ExpressionState &state, Vector &result) {
    SetConfigValue(args, state, result, "openprompt_api_url", "API URL");
}

static void SetModelName(DataChunk &args, ExpressionState &state, Vector &result) {
    SetConfigValue(args, state, result, "openprompt_model_name", "Model name");
}

// Main Function
static void OpenPromptRequestFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    D_ASSERT(args.data.size() >= 1); // At least promtp is required

    UnaryExecutor::Execute<string_t, string_t>(args.data[0], result, args.size(),
        [&](string_t user_prompt) {
            auto &context = state.GetContext();
            
            // Get configuration with defaults
            std::string api_url = GetConfigValue(context, "openprompt_api_url", 
                                               "http://localhost:11434/v1/chat/completions");
            std::string api_token = GetConfigValue(context, "openprompt_api_token", "");
            std::string model_name = GetConfigValue(context, "openprompt_model_name", "qwen2.5:0.5b");

            // Override model if provided as second argument
            if (args.data.size() > 1 && !args.data[1].GetValue(0).IsNull()) {
                model_name = args.data[1].GetValue(0).ToString();
            }

            // Construct request body
            std::string request_body = "{";
            request_body += "\"model\":\"" + model_name + "\",";
            request_body += "\"messages\":[";
            request_body += "{\"role\":\"system\",\"content\":\"You are a helpful assistant.\"},";
            request_body += "{\"role\":\"user\",\"content\":\"" + user_prompt.GetString() + "\"}";
            request_body += "]}";

            try {
                auto client_and_path = SetupHttpClient(api_url);
                auto &client = client_and_path.first;
                auto &path = client_and_path.second;

                // Setup headers
                duckdb_httplib_openssl::Headers headers;
                headers.emplace("Content-Type", "application/json");
                if (!api_token.empty()) {
                    headers.emplace("Authorization", "Bearer " + api_token);
                }

                auto res = client.Post(path.c_str(), headers, request_body, "application/json");
                
                if (!res) {
                    HandleHttpError(res, "POST");
                }
                
                if (res->status != 200) {
                    throw std::runtime_error("HTTP error " + std::to_string(res->status) + ": " + res->reason);
                }

                try {
                    unique_ptr<duckdb_yyjson::yyjson_doc, void(*)(duckdb_yyjson::yyjson_doc *)> doc(
                        duckdb_yyjson::yyjson_read(res->body.c_str(), res->body.length(), 0),
                        &duckdb_yyjson::yyjson_doc_free
                    );
                    
                    if (!doc) {
                        throw std::runtime_error("Failed to parse JSON response");
                    }

                    auto root = duckdb_yyjson::yyjson_doc_get_root(doc.get());
                    if (!root) {
                        throw std::runtime_error("Invalid JSON response: no root object");
                    }

                    auto choices = duckdb_yyjson::yyjson_obj_get(root, "choices");
                    if (!choices || !duckdb_yyjson::yyjson_is_arr(choices)) {
                        throw std::runtime_error("Invalid response format: missing choices array");
                    }

                    auto first_choice = duckdb_yyjson::yyjson_arr_get_first(choices);
                    if (!first_choice) {
                        throw std::runtime_error("Empty choices array in response");
                    }

                    auto message = duckdb_yyjson::yyjson_obj_get(first_choice, "message");
                    if (!message) {
                        throw std::runtime_error("Missing message in response");
                    }

                    auto content = duckdb_yyjson::yyjson_obj_get(message, "content");
                    if (!content) {
                        throw std::runtime_error("Missing content in response");
                    }

                    auto content_str = duckdb_yyjson::yyjson_get_str(content);
                    if (!content_str) {
                        throw std::runtime_error("Invalid content in response");
                    }

                    return StringVector::AddString(result, content_str);
                } catch (std::exception &e) {
                    throw std::runtime_error("Failed to parse response: " + std::string(e.what()));
                }
            } catch (std::exception &e) {
                // Log error and return error message
                return StringVector::AddString(result, "Error: " + std::string(e.what()));
            }
        });
}

static void LoadInternal(DatabaseInstance &instance) {
    ScalarFunctionSet open_prompt("open_prompt");
    
    // Register with both single and two-argument variants
    open_prompt.AddFunction(ScalarFunction(
        {LogicalType::VARCHAR}, LogicalType::VARCHAR, OpenPromptRequestFunction));
    open_prompt.AddFunction(ScalarFunction(
        {LogicalType::VARCHAR, LogicalType::VARCHAR}, LogicalType::VARCHAR, OpenPromptRequestFunction));
    
    ExtensionUtil::RegisterFunction(instance, open_prompt);

    // Register setting functions
    ExtensionUtil::RegisterFunction(instance, ScalarFunction(
        "set_api_token", {LogicalType::VARCHAR}, LogicalType::VARCHAR, SetApiToken));
    ExtensionUtil::RegisterFunction(instance, ScalarFunction(
        "set_api_url", {LogicalType::VARCHAR}, LogicalType::VARCHAR, SetApiUrl));
    ExtensionUtil::RegisterFunction(instance, ScalarFunction(
        "set_model_name", {LogicalType::VARCHAR}, LogicalType::VARCHAR, SetModelName));
}

void OpenPromptExtension::Load(DuckDB &db) {
    LoadInternal(*db.instance);
}

std::string OpenPromptExtension::Name() {
    return "open_prompt";
}

std::string OpenPromptExtension::Version() const {
#ifdef EXT_VERSION_OPENPROMPT
    return EXT_VERSION_OPENPROMPT;
#else
    return "";
#endif
}

} // namespace duckdb

extern "C" {
DUCKDB_EXTENSION_API void open_prompt_init(duckdb::DatabaseInstance &db) {
    duckdb::DuckDB db_wrapper(db);
    db_wrapper.LoadExtension<duckdb::OpenPromptExtension>();
}

DUCKDB_EXTENSION_API const char *open_prompt_version() {
    return duckdb::DuckDB::LibraryVersion();
}
}

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif
