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


namespace duckdb {

// Helper function to parse URL and setup client
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

    // Create client and set a reasonable timeout (e.g., 10 seconds)
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


// Open Prompt
// Global settings
    static std::string api_url = "http://localhost:11434/v1/chat/completions";
    static std::string api_token = "";  // Store your API token here
    static std::string model_name = "qwen2.5:0.5b";  // Default model
    static std::mutex settings_mutex;

    // Function to set API token
    void SetApiToken(const std::string &token) {
        std::lock_guard<std::mutex> guard(settings_mutex);
        if (token.empty()) {
            throw std::invalid_argument("API token cannot be empty.");
        }
        api_token = token;
        std::cerr << "API token set to: " << api_token << std::endl;  // Debugging output
    }

    // Function to set API URL
    void SetApiUrl(const std::string &url) {
        std::lock_guard<std::mutex> guard(settings_mutex);
        if (url.empty()) {
            throw std::invalid_argument("URL cannot be empty.");
        }
        api_url = url;
        std::cerr << "API URL set to: " << api_url << std::endl;  // Debugging output
    }

    // Function to set model name
    void SetModelName(const std::string &model) {
        std::lock_guard<std::mutex> guard(settings_mutex);
        if (model.empty()) {
            throw std::invalid_argument("Model name cannot be empty.");
        }
        model_name = model;
        std::cerr << "Model name set to: " << model_name << std::endl;  // Debugging output
    }

    // Retrieve the API URL from the stored settings
    static std::string GetApiUrl() {
        std::lock_guard<std::mutex> guard(settings_mutex);
        return api_url.empty() ? "http://localhost:11434/v1/chat/completions" : api_url;
    }

    // Retrieve the API token from the stored settings
    static std::string GetApiToken() {
        std::lock_guard<std::mutex> guard(settings_mutex);
        return api_token;
    }

    // Retrieve the model name from the stored settings
    static std::string GetModelName() {
        std::lock_guard<std::mutex> guard(settings_mutex);
        return model_name.empty() ? "qwen2.5:0.5b" : model_name;
    }

// Open Prompt Function
static void OpenPromptRequestFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    D_ASSERT(args.data.size() == 2); // Expecting the prompt and model name

    UnaryExecutor::Execute<string_t, string_t>(args.data[0], result, args.size(),
        [&](string_t user_prompt) {
            std::string api_url = GetApiUrl();  // Retrieve the API URL from settings
            std::string api_token = GetApiToken();  // Retrieve the API Token from settings
            std::string model_name;

            if (!args.data[1].GetValue(0).IsNull()) {
                model_name = args.data[1].GetValue(0).ToString();  // Use passed model name
            } else {
                model_name = GetModelName();  // Use the default model if none is provided
            }

            // Manually construct the JSON body as a string. TODO use json parser from extension.
            std::string request_body = "{";
            request_body += "\"model\":\"" + model_name + "\",";
            request_body += "\"messages\":[";
            request_body += "{\"role\":\"system\",\"content\":\"You are a helpful assistant.\"},";
            request_body += "{\"role\":\"user\",\"content\":\"" + user_prompt.GetString() + "\"}";
            request_body += "]}";

            try {
                // Make the POST request
                auto client_and_path = SetupHttpClient(api_url);
                auto &client = client_and_path.first;
                auto &path = client_and_path.second;

                // Setup headers
                duckdb_httplib_openssl::Headers header_map;
                header_map.emplace("Content-Type", "application/json");
                if (!api_token.empty()) {
                    header_map.emplace("Authorization", "Bearer " + api_token);
                }

                // Send the request
                auto res = client.Post(path.c_str(), header_map, request_body, "application/json");
                if (res && res->status == 200) {
                    // Extract the first choice's message content from the response
                    std::string response_body = res->body;
                    size_t choices_pos = response_body.find("\"choices\":");
                    if (choices_pos != std::string::npos) {
                        size_t message_pos = response_body.find("\"message\":", choices_pos);
                        size_t content_pos = response_body.find("\"content\":\"", message_pos);
                        if (content_pos != std::string::npos) {
                            content_pos += 11; // Move to the start of the content value
                            size_t content_end = response_body.find("\"", content_pos);
                            if (content_end != std::string::npos) {
                                std::string first_message_content = response_body.substr(content_pos, content_end - content_pos);
                                return StringVector::AddString(result, first_message_content);
                            }
                        }
                    }
                    throw std::runtime_error("Failed to parse the first message content in the API response.");
                } else {
                    throw std::runtime_error("HTTP POST error: " + std::to_string(res->status) + " - " + res->reason);
                }
            } catch (std::exception &e) {
                // In case of any error, return the original input text to avoid disruption
                return StringVector::AddString(result, user_prompt);
            }
        });
}


static void LoadInternal(DatabaseInstance &instance) {
    // Register open_prompt function with two arguments: prompt and model
    ScalarFunctionSet open_prompt("open_prompt");
    open_prompt.AddFunction(ScalarFunction(
        {LogicalType::VARCHAR, LogicalType::VARCHAR}, LogicalType::VARCHAR, OpenPromptRequestFunction));
    ExtensionUtil::RegisterFunction(instance, open_prompt);

    // Other set_* functions remain the same as before
    ExtensionUtil::RegisterFunction(instance, ScalarFunction(
        "set_api_token", {LogicalType::VARCHAR}, LogicalType::VARCHAR,
        [](DataChunk &args, ExpressionState &state, Vector &result) {
            try {
                auto token = args.data[0].GetValue(0).ToString();
                SetApiToken(token);
                return StringVector::AddString(result, "API token set successfully.");
            } catch (std::exception &e) {
                return StringVector::AddString(result, "Failed to set API token: " + std::string(e.what()));
            }
        }));

    ExtensionUtil::RegisterFunction(instance, ScalarFunction(
        "set_api_url", {LogicalType::VARCHAR}, LogicalType::VARCHAR,
        [](DataChunk &args, ExpressionState &state, Vector &result) {
            try {
                auto new_url = args.data[0].GetValue(0).ToString();
                SetApiUrl(new_url);
                return StringVector::AddString(result, "API URL set successfully.");
            } catch (std::exception &e) {
                return StringVector::AddString(result, "Failed to set API URL: " + std::string(e.what()));
            }
        }));

    ExtensionUtil::RegisterFunction(instance, ScalarFunction(
        "set_model_name", {LogicalType::VARCHAR}, LogicalType::VARCHAR,
        [](DataChunk &args, ExpressionState &state, Vector &result) {
            try {
                auto model = args.data[0].GetValue(0).ToString();
                SetModelName(model);
                return StringVector::AddString(result, "Model name set successfully.");
            } catch (std::exception &e) {
                return StringVector::AddString(result, "Failed to set model name: " + std::string(e.what()));
            }
        }));
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

