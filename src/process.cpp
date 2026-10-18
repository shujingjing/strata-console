#include "process.h"

#include <array>
#include <cmath>

namespace {

const int kBackoff[] = {2, 5, 10, 30, 60};

json slim_metrics(const json& m) {
    json out = json::object();
    if (!m.is_object()) return out;
    const char* keys[] = {"engine", "live", "hardware", "hardware_static",
                          "requests", "totals", "conversation_cache"};
    for (const char* k : keys) {
        auto it = m.find(k);
        if (it != m.end()) out[k] = *it;
    }
    return out;
}

bool health_ok(const json& h) {
    if (!h.is_object()) return false;
    if (h.contains("status") && h["status"].is_string()) {
        return h["status"].get<std::string>() == "ok" ||
               h["status"].get<std::string>() == "healthy";
    }
    return true;
}

std::vector<std::string> json_strings(const json& j, const char* key) {
    std::vector<std::string> out;
    if (!j.is_object()) return out;
    auto it = j.find(key);
    if (it == j.end()) return out;
    if (it->is_array()) {
        for (const auto& x : *it) if (x.is_string()) out.push_back(x.get<std::string>());
    } else if (it->is_string()) {
        out.push_back(it->get<std::string>());
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------- StatusCache

void StatusCache::start() {
    running_ = true;
    thread_ = std::thread([this] { loop(); });
}

void StatusCache::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void StatusCache::set_target(int port, const std::string& key, const std::string& log_path) {
    std::lock_guard<std::mutex> lk(m_);
    port_ = port;
    key_ = key;
    log_path_ = log_path;
    if (port <= 0) {
        health_ = json();
        metrics_ = json();
        error_.clear();
        last_log_.clear();
        updated_ = util::now_epoch();
        failures_ = 0;
    }
}

void StatusCache::loop() {
    while (running_) {
        int port = 0;
        std::string key, log_path;
        {
            std::lock_guard<std::mutex> lk(m_);
            port = port_;
            key = key_;
            log_path = log_path_;
        }
        if (port > 0) {
            int st = 0;
            std::string err;
            auto health = util::http_get_json(port, "/health", key, 800, &st, &err);
            auto metrics = util::http_get_json(port, "/metrics", key, 1200, &st, &err);
            std::string last = util::last_nonempty_line(log_path);
            std::lock_guard<std::mutex> lk(m_);
            if (health && health->is_object()) {
                health_ = *health;
                failures_ = 0;
            } else {
                failures_++;
                if (failures_ >= 3) {
                    health_ = json();
                    metrics_ = json();
                }
            }
            if (metrics && metrics->is_object()) metrics_ = slim_metrics(*metrics);
            if (!err.empty()) error_ = err;
            else if (health && metrics) error_.clear();
            if (!last.empty()) last_log_ = last;
            updated_ = util::now_epoch();
        }
        for (int i = 0; i < 10 && running_; ++i) Sleep(100);
    }
}

json StatusCache::state() {
    std::lock_guard<std::mutex> lk(m_);
    return json{{"port", port_},
                {"health", health_},
                {"metrics", metrics_},
                {"error", error_},
                {"last_log", last_log_},
                {"updated", updated_}};
}

std::string StatusCache::last_log() {
    std::lock_guard<std::mutex> lk(m_);
    return last_log_;
}

int StatusCache::port() {
    std::lock_guard<std::mutex> lk(m_);
    return port_;
}

// -------------------------------------------------------------- ProcessManager

ProcessManager::ProcessManager(ConfigStore& config, StatusCache& status)
    : config_(config), status_(status) {}

ProcessManager::~ProcessManager() {
    running_ = false;
    if (monitor_.joinable()) monitor_.join();
    if (proc_) {
        CloseHandle(proc_);
        proc_ = nullptr;
    }
}

void ProcessManager::reset_process_locked() {
    if (proc_) {
        CloseHandle(proc_);
        proc_ = nullptr;
    }
    pid_ = 0;
    owned_ = false;
    active_port_ = 0;
}

bool ProcessManager::start(bool lazy_override, bool is_restart, std::string& err) {
    {
        std::lock_guard<std::mutex> lk(m_);
        if (proc_) {
            DWORD wr = WaitForSingleObject(proc_, 0);
            if (wr == WAIT_TIMEOUT) { err = "模型服务已在运行"; return false; }
            reset_process_locked();
        }
        if (state_ == "restarting") {
            // allowed: a scheduled restart
        }
    }

    std::string root = config_.root();
    std::string config_path = config_.config_path();
    std::string key = config_.api_key();
    int port = config_.model_port();
    std::wstring cmd = config_.build_command(lazy_override);

    // The engine needs the CUDA toolkit runtime on PATH; the config's lib_dirs win too.
    std::vector<std::string> libs;
    std::string build_path = root + "\\engine\\BUILD.json";
    std::string read_err;
    auto build = util::read_json(build_path, read_err);
    if (build && build->is_object()) {
        for (const auto& d : json_strings(*build, "cuda_dirs")) libs.push_back(d);
    }
    auto cfg = util::read_json(config_path, read_err);
    if (cfg && cfg->is_object()) {
        for (const auto& d : json_strings(*cfg, "lib_dirs")) libs.push_back(d);
    }
    util::prepend_path(libs);
    SetEnvironmentVariableW(L"PYTHONUTF8", L"1");
    SetEnvironmentVariableW(L"PYTHONIOENCODING", L"utf-8");

    std::string log = root + "\\strata-console-server.log";
    std::error_code ec;
    fs::create_directories(util::path(root), ec);
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE log_h = CreateFileW(util::widen(log).c_str(), FILE_APPEND_DATA,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log_h == INVALID_HANDLE_VALUE) {
        err = "无法打开服务日志: " + log;
        return false;
    }
    {
        std::string marker = "\r\n===== Strata Console start " + util::now_iso() + " =====\r\n";
        DWORD written = 0;
        WriteFile(log_h, marker.data(), (DWORD)marker.size(), &written, nullptr);
    }
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = log_h;
    si.hStdError = log_h;
    si.hStdInput = nul;
    PROCESS_INFORMATION pi{};
    std::wstring mutable_cmd = cmd;
    std::wstring cwd = util::widen(root);
    BOOL ok = CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr,
                             cwd.c_str(), &si, &pi);
    DWORD create_err = GetLastError();
    CloseHandle(log_h);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) {
        err = "启动失败 (" + std::to_string(create_err) + "): " + util::narrow(cmd);
        std::lock_guard<std::mutex> lk(m_);
        state_ = "error";
        last_error_ = err;
        return false;
    }
    CloseHandle(pi.hThread);

    {
        std::lock_guard<std::mutex> lk(m_);
        proc_ = pi.hProcess;
        pid_ = pi.dwProcessId;
        owned_ = true;
        intentional_ = false;
        exit_code_ = 0;
        state_ = "starting";
        last_error_.clear();
        active_port_ = port;
        active_config_ = config_path;
        active_log_ = log;
        command_ = util::narrow(cmd);
        started_ = std::chrono::steady_clock::now();
        started_epoch_ = util::now_epoch();
        auto_restart_ = config_.auto_restart();
        lazy_on_restart_ = config_.lazy_on_restart();
        if (!is_restart) {
            restarts_ = 0;
            failures_ = 0;
            backoff_idx_ = 0;
        }
    }
    status_.set_target(port, key, log);
    return true;
}

bool ProcessManager::stop(bool unload_first, std::string& err) {
    HANDLE h = nullptr;
    int port = 0;
    std::string key;
    {
        std::lock_guard<std::mutex> lk(m_);
        if (!proc_) {
            state_ = "stopped";
            err = "模型服务未运行";
            return false;
        }
        h = proc_;
        port = active_port_;
        key = config_.api_key();
        intentional_ = true;
        state_ = "stopping";
    }

    if (unload_first && port > 0) {
        int code = 0;
        std::string e;
        util::http_post_json(port, "/v1/unload", key, json::object(), 2500, &code, &e);
    }

    TerminateProcess(h, 0);
    DWORD wr = WaitForSingleObject(h, 5000);
    if (wr == WAIT_TIMEOUT) {
        TerminateProcess(h, 1);
        WaitForSingleObject(h, 1000);
    }
    CloseHandle(h);
    for (int i = 0; i < 20 && port > 0 && util::tcp_port_open(port); ++i) Sleep(100);

    {
        std::lock_guard<std::mutex> lk(m_);
        proc_ = nullptr;
        pid_ = 0;
        owned_ = false;
        active_port_ = 0;
        state_ = "stopped";
        exit_code_ = 0;
        last_error_.clear();
        next_restart_ = {};
    }
    status_.set_target(0, key, "");
    return true;
}

bool ProcessManager::restart(bool lazy_override, std::string& err) {
    bool was_running = false;
    {
        std::lock_guard<std::mutex> lk(m_);
        was_running = proc_ != nullptr;
    }
    if (was_running) {
        std::string stop_err;
        stop(true, stop_err);
    }
    return start(lazy_override, false, err);
}

bool ProcessManager::load_model(std::string& err) {
    int port = 0;
    std::string key;
    {
        std::lock_guard<std::mutex> lk(m_);
        if (!proc_) { err = "模型服务未运行"; return false; }
        port = active_port_;
        key = config_.api_key();
    }
    int code = 0;
    std::string e;
    auto r = util::http_post_json(port, "/v1/load", key, json::object(), 120000, &code, &e);
    if (!r || code < 200 || code >= 300) {
        err = e.empty() ? "加载失败" : e;
        return false;
    }
    return true;
}

bool ProcessManager::unload_model(std::string& err) {
    int port = 0;
    std::string key;
    {
        std::lock_guard<std::mutex> lk(m_);
        if (!proc_) { err = "模型服务未运行"; return false; }
        port = active_port_;
        key = config_.api_key();
    }
    int code = 0;
    std::string e;
    auto r = util::http_post_json(port, "/v1/unload", key, json::object(), 5000, &code, &e);
    if (!r || code < 200 || code >= 300) {
        err = e.empty() ? "卸载失败" : e;
        return false;
    }
    return true;
}

void ProcessManager::tick() {
    bool do_restart = false;
    bool lazy = false;
    {
        std::lock_guard<std::mutex> lk(m_);
        if (proc_) {
            DWORD wr = WaitForSingleObject(proc_, 0);
            if (wr == WAIT_OBJECT_0) {
                DWORD code = 0;
                GetExitCodeProcess(proc_, &code);
                CloseHandle(proc_);
                proc_ = nullptr;
                pid_ = 0;
                owned_ = false;
                active_port_ = 0;
                exit_code_ = code;
                status_.set_target(0, "", "");
                if (intentional_) {
                    state_ = "stopped";
                    intentional_ = false;
                } else {
                    auto_restart_ = config_.auto_restart();
                    lazy_on_restart_ = config_.lazy_on_restart();
                    if (auto_restart_ && failures_ < 5) {
                        state_ = "restarting";
                        last_error_ = "服务退出，退出码 " + std::to_string(code);
                        failures_++;
                        int idx = std::min(backoff_idx_, (int)(std::size(kBackoff) - 1));
                        next_restart_ = std::chrono::steady_clock::now() +
                                        std::chrono::seconds(kBackoff[idx]);
                        backoff_idx_ = std::min(backoff_idx_ + 1,
                                                (int)(std::size(kBackoff) - 1));
                    } else {
                        state_ = "error";
                        last_error_ = "服务退出，退出码 " + std::to_string(code);
                    }
                }
            } else {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - started_).count();
                if (elapsed > 300 && failures_ != 0) {
                    failures_ = 0;
                    backoff_idx_ = 0;
                }
            }
        }
        if (!proc_ && state_ == "restarting" &&
            std::chrono::steady_clock::now() >= next_restart_) {
            do_restart = true;
            lazy = lazy_on_restart_;
        }
    }
    if (do_restart) {
        std::string e;
        if (!start(lazy, true, e)) {
            std::lock_guard<std::mutex> lk(m_);
            if (failures_ < 5) {
                state_ = "restarting";
                last_error_ = e;
                failures_++;
                int idx = std::min(backoff_idx_, (int)(std::size(kBackoff) - 1));
                next_restart_ = std::chrono::steady_clock::now() +
                                std::chrono::seconds(kBackoff[idx]);
                backoff_idx_ = std::min(backoff_idx_ + 1,
                                        (int)(std::size(kBackoff) - 1));
            } else {
                state_ = "error";
                last_error_ = e;
            }
        } else {
            std::lock_guard<std::mutex> lk(m_);
            restarts_++;
        }
    }
}

void ProcessManager::start_monitor() {
    running_ = true;
    monitor_ = std::thread([this] { monitor_loop(); });
}

void ProcessManager::monitor_loop() {
    while (running_) {
        tick();
        Sleep(500);
    }
}

void ProcessManager::shutdown() {
    running_ = false;
    if (monitor_.joinable()) monitor_.join();
}

json ProcessManager::status() {
    json sc = status_.state();
    std::string state;
    std::string detail;
    DWORD pid = 0;
    int uptime = 0;
    int exit_code = 0;
    int restarts = 0;
    int failures = 0;
    bool owned = false;
    bool auto_restart = false;
    bool lazy = true;
    std::string log, command, config;
    int active_port = 0;
    int next_in = 0;
    {
        std::lock_guard<std::mutex> lk(m_);
        owned = owned_ && proc_ != nullptr;
        pid = pid_;
        exit_code = (int)exit_code_;
        restarts = restarts_;
        failures = failures_;
        auto_restart = auto_restart_;
        lazy = lazy_on_restart_;
        log = active_log_;
        command = command_;
        config = active_config_;
        active_port = active_port_;
        state = state_;
        detail = last_error_;
        if (proc_) {
            uptime = (int)std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - started_).count();
        }
        if (state_ == "restarting") {
            auto left = std::chrono::duration_cast<std::chrono::seconds>(
                next_restart_ - std::chrono::steady_clock::now()).count();
            next_in = left > 0 ? (int)left : 0;
        }
    }

    json health = sc.value("health", json());
    json metrics = sc.value("metrics", json());
    bool hok = health.is_object() && health_ok(health);
    std::string live_state;
    if (metrics.is_object() && metrics.contains("live") && metrics["live"].is_object()) {
        live_state = metrics["live"].value("state", "");
    }

    if (owned) {
        if (state == "stopping") {
            // keep the stopping state until the monitor thread clears proc_
        } else if (hok) {
            bool loaded = health.value("loaded", false);
            if (loaded) state = "running";
            else if (live_state == "unloaded") state = "unloaded";
            else if (uptime < 180) state = "loading";
            else state = "unloaded";
        } else if (uptime < 30) {
            state = "starting";
        } else {
            state = "loading";
        }
    } else if (state == "starting" || state == "loading" || state == "running" ||
               state == "unloaded" || state == "stopping") {
        state = "stopped";
    } else if (state != "restarting" && state != "error") {
        state = "stopped";
    }
    if (!owned && state == "stopped") {
        int cfg_port = config_.model_port();
        if (cfg_port > 0 && util::tcp_port_open(cfg_port)) {
            state = "external";
            active_port = cfg_port;
        }
    }
    if (detail.empty()) {
        detail = sc.value("last_log", "");
    }

    return json{
        {"state", state},
        {"detail", detail},
        {"pid", pid},
        {"owned", owned},
        {"uptime_s", uptime},
        {"exit_code", exit_code},
        {"restarts", restarts},
        {"failures", failures},
        {"auto_restart", auto_restart},
        {"lazy_on_restart", lazy},
        {"log", log},
        {"command", command},
        {"config", config},
        {"active_port", active_port},
        {"next_restart_in", next_in},
        {"last_error", sc.value("error", "")},
        {"cache", sc}
    };
}

std::string ProcessManager::log_path() {
    std::lock_guard<std::mutex> lk(m_);
    return active_log_;
}
