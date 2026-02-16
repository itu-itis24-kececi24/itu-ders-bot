# ITU Course Picker (Native C++)

A high-precision, low-latency course registration tool for İstanbul Technical University (İTÜ) course selection. This project is built in native C++ using **WinHTTP** to achieve the fastest possible request execution during the registration time.

## 🚀 Features

* **Native Performance**: Bypasses heavy browser engines and uses winhttp requests for minimal execution overhead.
* **Clock Synchronization**: Samples latency from İTÜ servers (5-pass average) to calculate clock drift and fire requests with millisecond precision.
* **Smart Handshake**: Handles İTÜ's multi-domain authentication (girisv3) and identity selection natively.
* **Precision Timing**: Uses a hybrid Sleep/Spin loop to fire requests at the exact moment the registration window opens.

## 🛠️ Build Instructions

### Prerequisites
* Windows 10/11
* MinGW-w64 (UCRT64 recommended) or MSVC
* `winhttp.lib` (System library)

### Compilation
This project requires MinGW-w64 (UCRT64 recommended) and the WinHTTP system library.
Use the following command to create the executable:

```bash
g++ -O3 src/main.cpp src/clock.cpp src/token.cpp -I include \
    -o program.exe \
    -lwinhttp
```
## ⚙️ Configuration
The program reads user credentials and target courses from `data/config.json`. Ensure this file exists in the same directory as the executable.

```json
{
  "account": { 
    "username": "itucanarioglu", 
    "password": "çokgizlişifre" 
  },
  "time": { 
    "year": 2026, 
    "month": 1, 
    "day": 1, 
    "hour": 10, 
    "minute": 0 
  },
  "courses": { 
    "crn": ["11111", "11112"],
    "scrn": ["11113"] 
  }
}
```

## 🖥️ Command Line Flags
* `-logs`: Enables verbose logging of HTML responses and JWT acquisition.

* `-test`: Skips the clock wait and fires immediately (useful for testing logic).

* `-local`: Skips server clock synchronization and relies on the local system time.

## 📅 To-Do List / Roadmap
- [ ] Create a cmake file.
- [ ] Create a "log file" system to save registration history.
- [ ] Setup helper for initialization of config file.

## 🤝 Acknowledgments & Credits
* Logic Attribution: Server result code mappings and config.json were adapted and ported to C++ from the original Python implementation by [AtaTrkgl](https://github.com/AtaTrkgl/itu-ders-secici).

* [nlohmann/json](https://github.com/nlohmann/json): For modern, header-only JSON parsing in C++.

### ⚠️ Disclaimer
This tool is developed for educational purposes and personal use. The developers are not responsible for any misuse of this software or any consequences resulting from its use during the İTÜ registration process. Please use it responsibly and in compliance with university regulations.