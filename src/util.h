#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "httplib.h"
#include "nlohmann/json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace util {

inline std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

inline std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

inline fs::path path(const std::string& utf8) { return fs::path(widen(utf8)); }
inline std::string u8(const fs::path& p) { return narrow(p.wstring()); }

inline std::string trim(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

inline std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    std::stringstream ss(s);
    while (std::getline(ss, cur, sep)) out.push_back(trim(cur));
    return out;
}

inline std::string join(const std::vector<std::string>& v, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += sep;
        out += v[i];
    }
    return out;
}

inline std::string now_iso() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char buf[64]{};
    sprintf_s(buf, "%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay,
              st.wHour, st.wMinute, st.wSecond);
    return buf;
}

inline std::string timestamp_compact() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char buf[32]{};
    sprintf_s(buf, "%04d%02d%02d-%02d%02d%02d", st.wYear, st.wMonth, st.wDay,
              st.wHour, st.wMinute, st.wSecond);
    return buf;
}

inline int64_t now_epoch() {
    return (int64_t)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

inline std::string random_hex(size_t bytes) {
    std::random_device rd;
    std::mt19937_64 gen(((uint64_t)rd() << 32) ^ rd() ^ (uint64_t)GetTickCount64());
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(bytes * 2);
    for (size_t i = 0; i < bytes; ++i) {
        unsigned v = (unsigned)(gen() & 0xff);
        out.push_back(hex[v >> 4]);
        out.push_back(hex[v & 15]);
    }
    return out;
}

inline bool path_exists(const std::string& p) {
    std::error_code ec;
    return !p.empty() && fs::exists(path(p), ec);
}

inline bool is_dir(const std::string& p) {
    std::error_code ec;
    return !p.empty() && fs::is_directory(path(p), ec);
}

inline bool is_file(const std::string& p) {
    std::error_code ec;
    return !p.empty() && fs::is_regular_file(path(p), ec);
}

inline std::string file_size_str(const std::string& p) {
    std::error_code ec;
    auto n = fs::file_size(path(p), ec);
    if (ec) return "-";
    char buf[64]{};
    if (n >= 1024ull * 1024 * 1024) sprintf_s(buf, "%.1f GB", n / 1073741824.0);
    else if (n >= 1024ull * 1024) sprintf_s(buf, "%.1f MB", n / 1048576.0);
    else if (n >= 1024) sprintf_s(buf, "%.1f KB", n / 1024.0);
    else sprintf_s(buf, "%llu B", (unsigned long long)n);
    return buf;
}

inline std::optional<json> read_json(const std::string& p, std::string& err) {
    try {
        std::ifstream f(path(p), std::ios::binary);
        if (!f) { err = "cannot open " + p; return std::nullopt; }
        json j;
        f >> j;
        if (!f.good() && !f.eof()) { err = "invalid JSON in " + p; return std::nullopt; }
        return j;
    } catch (const std::exception& e) {
        err = e.what();
        return std::nullopt;
    }
}

inline bool write_json_atomic(const std::string& p, const json& j, std::string& err) {
    try {
        fs::path target = path(p);
        std::error_code ec;
        if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
        fs::path tmp = target;
        tmp += L".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f) { err = "cannot write " + u8(tmp); return false; }
            f << j.dump(2, ' ', false, json::error_handler_t::replace);
            f << "\n";
            if (!f.good()) { err = "write failed " + u8(tmp); return false; }
        }
        if (!MoveFileExW(tmp.wstring().c_str(), target.wstring().c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            err = "replace failed: " + std::to_string(GetLastError());
            DeleteFileW(tmp.wstring().c_str());
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

inline std::vector<std::string> tail_lines(const std::string& p, int max_lines,
                                           size_t max_bytes = 262144) {
    std::vector<std::string> lines;
    if (p.empty()) return lines;
    std::ifstream f(path(p), std::ios::binary | std::ios::ate);
    if (!f) return lines;
    auto end = f.tellg();
    if (end <= 0) return lines;
    size_t size = (size_t)end;
    size_t start = size > max_bytes ? size - max_bytes : 0;
    f.seekg((std::streamoff)start);
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (start > 0) {
        auto nl = data.find('\n');
        if (nl != std::string::npos) data.erase(0, nl + 1);
    }
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF &&
        (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) {
        data.erase(0, 3);
    }
    std::stringstream ss(data);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    if ((int)lines.size() > max_lines) lines.erase(lines.begin(), lines.end() - max_lines);
    return lines;
}

inline std::string last_nonempty_line(const std::string& p) {
    auto lines = tail_lines(p, 40, 65536);
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        if (!trim(*it).empty()) return *it;
    }
    return {};
}

inline std::wstring quote_arg(const std::wstring& s) {
    if (s.find_first_of(L" \t\n\v\"") == std::wstring::npos) return s;
    std::wstring out = L"\"";
    for (size_t i = 0; i < s.size(); ++i) {
        size_t backslashes = 0;
        while (i < s.size() && s[i] == L'\\') { ++backslashes; ++i; }
        if (i == s.size()) {
            out.append(backslashes * 2, L'\\');
            break;
        }
        if (s[i] == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
        } else {
            out.append(backslashes, L'\\');
            out.push_back(s[i]);
        }
    }
    out.push_back(L'"');
    return out;
}

struct CaptureResult {
    std::string out;
    std::string err;
    DWORD code = 0;
    bool timed_out = false;
};

inline CaptureResult run_capture(const std::wstring& cmd, const std::wstring& cwd,
                                 DWORD timeout_ms) {
    CaptureResult result;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE out_r = nullptr, out_w = nullptr, err_r = nullptr, err_w = nullptr;
    if (!CreatePipe(&out_r, &out_w, &sa, 0) || !CreatePipe(&err_r, &err_w, &sa, 0)) {
        result.err = "CreatePipe failed";
        return result;
    }
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = out_w;
    si.hStdError = err_w;
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &sa, OPEN_EXISTING, 0, nullptr);
    si.hStdInput = nul;
    PROCESS_INFORMATION pi{};
    std::wstring mutable_cmd = cmd;
    BOOL ok = CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr,
                             cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    CloseHandle(out_w);
    CloseHandle(err_w);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) {
        CloseHandle(out_r);
        CloseHandle(err_r);
        result.err = "CreateProcess failed: " + std::to_string(GetLastError());
        return result;
    }
    auto started = std::chrono::steady_clock::now();
    auto read_all = [](HANDLE h, std::string& dst) {
        char buf[4096];
        DWORD avail = 0, got = 0;
        while (PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            DWORD to_read = std::min<DWORD>(avail, sizeof(buf));
            if (!ReadFile(h, buf, to_read, &got, nullptr) || got == 0) break;
            dst.append(buf, got);
        }
    };
    while (true) {
        read_all(out_r, result.out);
        read_all(err_r, result.err);
        DWORD wr = WaitForSingleObject(pi.hProcess, 20);
        if (wr == WAIT_OBJECT_0) break;
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        if ((DWORD)elapsed > timeout_ms) {
            result.timed_out = true;
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 1000);
            break;
        }
    }
    read_all(out_r, result.out);
    read_all(err_r, result.err);
    GetExitCodeProcess(pi.hProcess, &result.code);
    CloseHandle(out_r);
    CloseHandle(err_r);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return result;
}

inline httplib::Headers auth_headers(const std::string& key) {
    httplib::Headers h;
    if (!key.empty()) h.emplace("Authorization", "Bearer " + key);
    return h;
}

inline std::optional<json> http_get_json(int port, const std::string& path,
                                         const std::string& key, int timeout_ms,
                                         int* status = nullptr, std::string* err = nullptr) {
    try {
        httplib::Client cli("127.0.0.1", port);
        cli.set_connection_timeout(0, timeout_ms * 1000);
        cli.set_read_timeout(0, timeout_ms * 1000);
        cli.set_write_timeout(0, timeout_ms * 1000);
        auto res = cli.Get(path.c_str(), auth_headers(key));
        if (status) *status = res ? res->status : 0;
        if (!res) {
            if (err) *err = httplib::to_string(res.error());
            return std::nullopt;
        }
        if (res->status < 200 || res->status >= 300) {
            if (err) *err = "HTTP " + std::to_string(res->status);
            return std::nullopt;
        }
        return json::parse(res->body);
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        return std::nullopt;
    }
}

inline std::optional<json> http_post_json(int port, const std::string& path,
                                          const std::string& key, const json& body,
                                          int timeout_ms, int* status = nullptr,
                                          std::string* err = nullptr) {
    try {
        httplib::Client cli("127.0.0.1", port);
        cli.set_connection_timeout(0, timeout_ms * 1000);
        cli.set_read_timeout(0, timeout_ms * 1000);
        cli.set_write_timeout(0, timeout_ms * 1000);
        auto res = cli.Post(path.c_str(), auth_headers(key), body.dump(),
                            "application/json");
        if (status) *status = res ? res->status : 0;
        if (!res) {
            if (err) *err = httplib::to_string(res.error());
            return std::nullopt;
        }
        json j = json::object();
        if (!res->body.empty()) {
            try { j = json::parse(res->body); } catch (...) { j["raw"] = res->body; }
        }
        if (res->status < 200 || res->status >= 300) {
            if (err) *err = j.value("error", "HTTP " + std::to_string(res->status));
            return j;
        }
        return j;
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        return std::nullopt;
    }
}

inline bool tcp_port_open(int port) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    bool open = false;
    if (connect(s, (sockaddr*)&addr, sizeof(addr)) == 0) {
        open = true;
    } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
        fd_set wf, ef;
        FD_ZERO(&wf); FD_ZERO(&ef);
        FD_SET(s, &wf); FD_SET(s, &ef);
        timeval tv{0, 250000};
        int r = select(0, nullptr, &wf, &ef, &tv);
        if (r > 0 && FD_ISSET(s, &wf)) {
            int soerr = 0, len = sizeof(soerr);
            getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&soerr, &len);
            open = (soerr == 0);
        }
    }
    closesocket(s);
    return open;
}

inline bool is_loopback_host(const std::string& h) {
    return h == "127.0.0.1" || h == "localhost" || h == "::1" || h == "[::1]";
}

inline std::vector<std::string> local_ipv4s() {
    std::vector<std::string> out;
    char host[256]{};
    if (gethostname(host, sizeof(host)) != 0) return out;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host, nullptr, &hints, &res) != 0) return out;
    for (addrinfo* p = res; p; p = p->ai_next) {
        char buf[INET_ADDRSTRLEN]{};
        auto* a = (sockaddr_in*)p->ai_addr;
        if (inet_ntop(AF_INET, &a->sin_addr, buf, sizeof(buf))) {
            std::string ip = buf;
            if (ip.rfind("127.", 0) != 0 && std::find(out.begin(), out.end(), ip) == out.end())
                out.push_back(ip);
        }
    }
    freeaddrinfo(res);
    auto score = [](const std::string& ip) {
        if (ip.rfind("192.168.", 0) == 0) return 0;
        if (ip.rfind("10.", 0) == 0) return 1;
        if (ip.rfind("172.", 0) == 0 && ip.size() > 4) {
            int second = 0;
            try { second = std::stoi(ip.substr(4)); } catch (...) {}
            if (second >= 16 && second <= 31) return 2;
        }
        return 3;
    };
    std::sort(out.begin(), out.end(), [&](const std::string& a, const std::string& b) {
        return score(a) < score(b);
    });
    return out;
}

inline void open_url(const std::string& url) {
    ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

inline void open_path(const std::string& p) {
    if (p.empty()) return;
    if (is_dir(p)) ShellExecuteW(nullptr, L"open", widen(p).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    else ShellExecuteW(nullptr, L"open", L"notepad.exe", widen("\"" + p + "\"").c_str(),
                       nullptr, SW_SHOWNORMAL);
}

inline std::string get_env_utf8(const wchar_t* name) {
    DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (!n) return {};
    std::wstring w(n, L'\0');
    GetEnvironmentVariableW(name, w.data(), n);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return narrow(w);
}

inline void prepend_path(const std::vector<std::string>& dirs) {
    if (dirs.empty()) return;
    std::wstring old = widen(get_env_utf8(L"PATH"));
    std::wstring pref;
    for (const auto& d : dirs) {
        if (d.empty()) continue;
        if (!pref.empty()) pref += L";";
        pref += widen(d);
    }
    if (pref.empty()) return;
    if (!old.empty()) pref += L";" + old;
    SetEnvironmentVariableW(L"PATH", pref.c_str());
}

inline std::string exe_dir() {
    wchar_t buf[MAX_PATH * 4]{};
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
    if (!n) return ".";
    fs::path p(buf, buf + n);
    return u8(p.parent_path());
}

}  // namespace util
