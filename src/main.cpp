#include <windows.h>
#include <winhttp.h>
#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <thread>
#include <vector>
#include "clock.hpp"
#include "token.hpp"
#include "response.hpp"
#include "../include/nlohmann_json.hpp"

#pragma comment(lib, "winhttp.lib")

using json = nlohmann::json;

struct ConfigFlags {
    bool debug;
    bool test;
    bool local;
};

int main(int argc, char *argv[]) {
    SetConsoleOutputCP(65001); // Allow unicode characters on console

    // Configure program flags
    const ConfigFlags flags = [argc, argv](){
        bool d = false, t = false, l = false;

        for(int i = 1; i < argc; i++){
            std::string arg = argv[i];
            if(arg == "-logs") d = true;
            if(arg == "-test") t = true;
            if(arg == "-local") l = false;
        }

        return ConfigFlags{d, t, l};
    }();

    // Load Configuration
    std::ifstream config_file("data/config.json");
    if (!config_file.is_open()) {
        std::cerr << "[Fatal] data/config.json not found." << std::endl;
        return 1;
    }
    json config;
    config_file >> config;

    // Initialize Helpers
    SystemClock itu_clock;
    TokenFetcher itu_auth;

    // Setup Persistent Session with Chrome User-Agent
    HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/144.0.0.0 Safari/537.36", 
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, 
                                    WINHTTP_NO_PROXY_NAME, 
                                    WINHTTP_NO_PROXY_BYPASS, 0);

    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    HINTERNET hConnect = WinHttpConnect(hSession, L"obs.itu.edu.tr", INTERNET_DEFAULT_HTTPS_PORT, 0);

    if (!hConnect) {
        std::cerr << "[Fatal] Could not connect to servers. Error: " << GetLastError() << std::endl;
        return 1;
    }

    // Initial Clock Sync
    if(!flags.local){
        itu_clock.sync_with_server(hConnect);
    }else{
        std::cout << "[Clock] Skipping server synchronization." << std::endl;
    }
    
    // Calculate Target Time Points
    auto t = config["time"];
    std::tm target_tm = {};
    target_tm.tm_year = t["year"].get<int>() - 1900;
    target_tm.tm_mon  = t["month"].get<int>() - 1;
    target_tm.tm_mday = t["day"].get<int>();
    target_tm.tm_hour = t["hour"].get<int>();
    target_tm.tm_min  = t["minute"].get<int>();
    target_tm.tm_sec  = 0;

    auto target_tp = std::chrono::system_clock::from_time_t(std::mktime(&target_tm));
    auto sync_tp = target_tp - std::chrono::seconds(90);
    auto token_tp = target_tp - std::chrono::seconds(60);

    if(flags.test) std::cout << "[Warning] Test mode enabled, immediately sending request" << std::endl;

    if(std::chrono::system_clock::now() < sync_tp && !flags.test && !flags.local){
        std::cout << "[System] Wait until 90s..." << std::endl;
        std::this_thread::sleep_until(sync_tp);
        
        std::cout << "[Clock] Re-Sync with ITU Server..." << std::endl;
        itu_clock.sync_with_server(hConnect);
    }
    else if(!flags.local){
        std::cout << "[Warning] Less than 90s remains. Skipping resync..." << std::endl;
    }

    // Wait for Pre-Fetch Phase
    if(!flags.test){
        std::cout << "[System] Waiting until 60s before target for native token acquisition..." << std::endl;
        std::this_thread::sleep_until(token_tp);
    }

    // Acquire Token 
    std::string auth_header = itu_auth.get_bearer_token(
        config["account"]["username"], 
        config["account"]["password"],
        flags.debug // Print extra logs if debug is true
    );

    if (auth_header.find("ERROR") != std::string::npos) {
        std::cerr << "[Critical] " << auth_header << std::endl;
        return 1;
    }
    std::cout << "[Success] JWT acquired." << std::endl;
    if(flags.debug) std::cout << "[Debug] Auth token: \n" << auth_header << std::endl;

    // Prepare Request Payload (Ensure ECRN/SCRN are arrays)
    std::cout << "[JSON] Preparing add CRN" << std::endl;
    json body_json;
    body_json["ECRN"] = json::array();
    for (auto& item : config["courses"]["crn"]){
        std::cout << "   Adding: " << item.get<std::string>() << std::endl;
        body_json["ECRN"].push_back(item);
    }
    
    std::cout << "[JSON] Preparing drop CRN" << std::endl;
    body_json["SCRN"] = json::array();
    if (config["courses"].contains("scrn")) {
        for (auto& item : config["courses"]["scrn"]){
            std::cout << "   Dropping: " << item.get<std::string>() << std::endl;
            body_json["SCRN"].push_back(item);
        }
    }
    
    std::string body_data = body_json.dump();
    std::wstring wToken(auth_header.begin(), auth_header.end());

    // Build Comprehensive Headers (Browser Fetch)
    std::wstring headers = 
        L"Authorization: " + wToken + L"\r\n" +
        L"Content-Type: application/json\r\n" +
        L"Accept: application/json, text/plain, */*\r\n" +
        L"Accept-Language: tr-TR,tr;q=0.9,en-US;q=0.8,en;q=0.7\r\n" +
        L"Referer: https://obs.itu.edu.tr/ogrenci/DersKayitIslemleri/DersKayit\r\n" +
        L"sec-ch-ua: \"Not(A:Brand\";v=\"8\", \"Chromium\";v=\"144\", \"Google Chrome\";v=\"144\"\r\n" +
        L"sec-ch-ua-mobile: ?0\r\n" +
        L"sec-ch-ua-platform: \"Windows\"\r\n" +
        L"sec-fetch-dest: empty\r\n" +
        L"sec-fetch-mode: cors\r\n" +
        L"sec-fetch-site: same-origin\r\n";

    // Final Wait
    if(!flags.test) itu_clock.wait_until(t["year"], t["month"], t["day"], t["hour"], t["minute"]);

    // Send registration request
    std::cout << ">>> FIRING REGISTRATION REQUEST <<<" << std::endl;

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/api/ders-kayit/v21", 
                                            NULL, WINHTTP_NO_REFERER, 
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, 
                                            WINHTTP_FLAG_SECURE);

    if (!WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)-1L, 
                           (LPVOID)body_data.c_str(), (DWORD)body_data.length(), 
                           (DWORD)body_data.length(), 0)) {
        DWORD err = GetLastError();
        std::cerr << "[Error] WinHttpSendRequest failed: " << err << std::endl;

        if(err == ERROR_WINHTTP_CANNOT_CONNECT) std::cerr << "Cannot connect to server." << std::endl;
        if(err == ERROR_WINHTTP_SECURE_FAILURE) std::cerr << "SSL/TLS handshake error." << std::endl;
    } else {
        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            std::cerr << "[Error] WinHttpReceiveResponse failed: " << GetLastError() << std::endl;
        } else {
            DWORD code = 0, size = sizeof(code);
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, 
                                WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
            std::cout << "[Result] Server Response Code: " << code << std::endl;

            std::string response_raw;
            DWORD dwSize = 0;
            do{
                WinHttpQueryDataAvailable(hRequest, &dwSize);
                if(dwSize == 0) break;
                std::vector<char> buffer(dwSize);
                DWORD dwDownloaded = 0;
                WinHttpReadData(hRequest, (LPVOID)&buffer[0], dwSize, &dwDownloaded);
                response_raw.append(buffer.data(), dwDownloaded);
            }while(dwSize > 0);

            if(flags.debug) std::cout << "[Debug] Raw Response: \n" << response_raw << std::endl;

            // Parse response to json
            /** TODO: handle scrn as well */
            try{
                auto res_json = json::parse(response_raw);

                std::cout << "\n--- Registration Results ---" << std::endl;
                for(const auto& item : res_json["ecrnResultList"]){
                    std::string crn = item["crn"].get<std::string>();
                    std::string code = item["resultCode"].get<std::string>();

                    std::cout << get_result_message(code, crn) << std::endl;
                }

            }catch(json::parse_error &e){
                std::cerr << "[Error] Failed to parse response JSON: " << e.what() << std::endl;
                std::cerr << "Raw response: \n" << response_raw << std::endl;
            }
        }
    }

    // Cleanup
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    std::cout << "[System] Press Enter to exit." << std::endl;
    std::cin.get();
    return 0;

}
