#include "clock.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <vector>

SystemClock::SystemClock() : offset_ms(0) {}

const int samples = 5; // Take the average of n probes to filter out jitter 

std::time_t SystemClock::parse_http_date(const std::wstring& date_str) {
    std::tm tm = {};
    std::wistringstream ss(date_str);
    // Format example: Sat, 07 Feb 2026 14:00:01 GMT
    // Note: Windows implementation of get_time can be locale-sensitive.
    // For robustness in this specific format, simple parsing is often safer, 
    // but get_time works if locale is "C".
    ss.imbue(std::locale("C")); 
    ss >> std::get_time(&tm, L"%a, %d %b %Y %H:%M:%S");
    return _mkgmtime(&tm);
}

void SystemClock::sync_with_server(HINTERNET hConnect) {
    std::cout << "[Clock] Syncing with ITU server..." << std::endl;

    long long total_offset = 0;

    for (int i = 0; i < samples; i++) {
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"HEAD", L"/", 
                                                NULL, WINHTTP_NO_REFERER, 
                                                WINHTTP_DEFAULT_ACCEPT_TYPES, 
                                                WINHTTP_FLAG_SECURE);
        
        auto t1 = std::chrono::system_clock::now();
        if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(hRequest, NULL)) {
            
            auto t2 = std::chrono::system_clock::now();
            
            wchar_t date_buffer[256];
            DWORD dwSize = sizeof(date_buffer);
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CUSTOM, L"Date", date_buffer, &dwSize, WINHTTP_NO_HEADER_INDEX);

            std::time_t server_time_t = parse_http_date(date_buffer);
            // Server time is in seconds precision, so we treat it as X.000s
            // Ideally, we assume the server generated this at the midpoint of our request
            auto server_time_pt = std::chrono::system_clock::from_time_t(server_time_t);
            auto mid_point_local = t1 + (t2 - t1) / 2;

            auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(server_time_pt - mid_point_local).count();
            total_offset += diff;
            
            std::wcout << L"   Sample " << (i+1) << ": Server Date [" << date_buffer << L"] Offset: " << diff << "ms" << std::endl;
        }
        WinHttpCloseHandle(hRequest);
        Sleep(500); // Wait a bit between probes
    }

    this->offset_ms = total_offset / samples;
    std::cout << "[Clock] Final Average Offset: " << this->offset_ms << "ms (Positive means Local is SLOWER)" << std::endl;
}

void SystemClock::wait_until(int year, int month, int day, int hour, int minute, int second) {
    std::tm target_tm = {};
    target_tm.tm_year = year - 1900;
    target_tm.tm_mon  = month - 1;
    target_tm.tm_mday = day;
    target_tm.tm_hour = hour;
    target_tm.tm_min  = minute;
    target_tm.tm_sec  = second;

    auto target_tp = std::chrono::system_clock::from_time_t(std::mktime(&target_tm));
    
    // Adjust target by offset AND ping buffer
    // If offset is +1000ms (Server is ahead), we must fire when local clock is 13:59:59 to match server 14:00:00
    // So we subtract the offset from the wait duration (or effectively shift the target local time)
    
    std::cout << "[Clock] Waiting for target time..." << std::endl;

    while (true) {
        auto now_local = std::chrono::system_clock::now();
        
        // Calculate "Server Time" based on our local clock + offset
        auto now_server_estimated = now_local + std::chrono::milliseconds(offset_ms);
        
        // How many ms until the server hits the target?
        auto ms_remaining = std::chrono::duration_cast<std::chrono::milliseconds>(target_tp - now_server_estimated).count();

        // Fire slightly early (PING_BUFFER_MS) to hit the server exactly on time
        if (ms_remaining <= PING_BUFFER_MS) {
            return; 
        }

        // Hybrid Sleep/Spin
        if (ms_remaining > 2000) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        } else if (ms_remaining > 50) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        // < 50ms: Busy spin (consume CPU for max precision)
    }
}