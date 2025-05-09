#include <iostream>
#include <string>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <filesystem>

using json = nlohmann::json;

// Helper to keep partial lines and the temp‐file stream
struct StreamProcessor {
    std::string buffer;
    std::ofstream* temp_out;
    bool done = false;
};

// Callback function to handle streaming data
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* sp = static_cast<StreamProcessor*>(userp);
    size_t total_size = size * nmemb;
    sp->buffer.append(static_cast<char*>(contents), total_size);

    // Extract full lines
    size_t pos;
    while ((pos = sp->buffer.find('\n')) != std::string::npos) {
        std::string line = sp->buffer.substr(0, pos);
        sp->buffer.erase(0, pos + 1);

        // Only process lines starting with "data: "
        if (line.rfind("data: ", 0) == 0) {
            std::string json_data = line.substr(6);
            if (json_data == "[DONE]") {
                sp->done = true;
                break;
            }
            try {
                auto parsed = json::parse(json_data);
                if (parsed.contains("choices") && !parsed["choices"].empty()) {
                    auto delta = parsed["choices"][0]["delta"];
                    if (delta.contains("content") && !delta["content"].is_null()) {
                        std::string content = delta["content"].get<std::string>();
                        // write to temp file
                        *sp->temp_out << content;
                        // live print to console
                        std::cout << content << std::flush;
                    }
                }
            } catch (const json::exception& e) {
                std::cerr << "\nJSON parsing error: " << e.what() << std::endl;
            }
        }
    }
    return total_size;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <prompt>" << std::endl;
        return 1;
    }

    const std::string API_URL = "https://api.xeduapi.com/v1/chat/completions";
    const std::string API_KEY = "sk-ecyR1xHxvhGDfaj87d3e6aF12eCf475bA31d49D87015138b";  // keep your key secure!
    const std::string PROMPT  = argv[1];

    // Prepare temp file
    auto temp_file = std::filesystem::temp_directory_path() / "api_response.txt";
    std::ofstream temp_out(temp_file, std::ios::binary);
    if (!temp_out) {
        std::cerr << "Failed to open temp file for writing\n";
        return 1;
    }

    // Build JSON payload
    json payload = {
        {"model", "gpt-4o"},
        {"messages", {{{"role", "user"}, {"content", PROMPT}}}},
        {"stream", true}
    };
    std::string payload_str = payload.dump();

    // Init CURL
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "CURL initialization failed\n";
        return 1;
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + API_KEY).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    // Set up our StreamProcessor and pass to CURL
    StreamProcessor sp{ "", &temp_out, false };
    curl_easy_setopt(curl, CURLOPT_URL,             API_URL.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,      headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,      payload_str.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,   WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,       &sp);

    // Perform request (this will call WriteCallback repeatedly)
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK && !sp.done) {
        std::cerr << "\nCURL request failed: " << curl_easy_strerror(res) << std::endl;
    }

    // Clean up CURL
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    temp_out.close();

    // Optionally, you can still read & print the full assembled file here
    // (but since we've already streamed to stdout, you probably don't need to)
    // std::ifstream temp_in(temp_file, std::ios::binary);
    // std::cout << "\n\nFull response:\n" << temp_in.rdbuf() << std::endl;

    // Remove the temp file
    std::filesystem::remove(temp_file);
    return 0;
}
