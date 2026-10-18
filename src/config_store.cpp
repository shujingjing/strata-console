#include "config_store.h"

#include <chrono>
#include <cmath>
#include <set>

namespace {

std::string sval(const json& j, const char* k, const std::string& def = "") {
    if (!j.is_object()) return def;
    auto it = j.find(k);
    if (it == j.end() || it->is_null()) return def;
    if (it->is_string()) return it->get<std::string>();
    if (it->is_number_integer()) return std::to_string(it->get<long long>());
    if (it->is_number()) return std::to_string(it->get<double>());
    return def;
}

int ival(const json& j, const char* k, int def = 0) {
    if (!j.is_object()) return def;
    auto it = j.find(k);
    if (it == j.end() || it->is_null()) return def;
    try {
        if (it->is_number_integer()) return it->get<int>();
        if (it->is_number()) return (int)it->get<double>();
        if (it->is_string()) return std::stoi(it->get<std::string>());
    } catch (...) {}
    return def;
}

bool bval(const json& j, const char* k, bool def = false) {
    if (!j.is_object()) return def;
    auto it = j.find(k);
    if (it == j.end() || it->is_null()) return def;
    if (it->is_boolean()) return it->get<bool>();
    if (it->is_number()) return it->get<double>() != 0;
    return def;
}

double dval(const json& j, const char* k, double def = 0) {
    if (!j.is_object()) return def;
    auto it = j.find(k);
    if (it == j.end() || it->is_null()) return def;
    try {
        if (it->is_number()) return it->get<double>();
        if (it->is_string()) return std::stod(it->get<std::string>());
    } catch (...) {}
    return def;
}

std::string list_text(const json& j) {
    if (j.is_string()) return j.get<std::string>();
    if (j.is_array()) {
        std::vector<std::string> v;
        for (const auto& x : j) if (x.is_string()) v.push_back(x.get<std::string>());
        return util::join(v, ", ");
    }
    return {};
}

json text_list(const std::string& text) {
    json a = json::array();
    for (const auto& p : util::split(text, ',')) if (!p.empty()) a.push_back(p);
    return a;
}

std::string gpu_text(const json& cfg) {
    auto it = cfg.find("gpu");
    if (it == cfg.end()) it = cfg.find("gpus");
    if (it == cfg.end()) return {};
    if (it->is_array()) {
        std::vector<std::string> v;
        for (const auto& x : *it) if (x.is_number_integer()) v.push_back(std::to_string(x.get<int>()));
        return util::join(v, ",");
    }
    if (it->is_number_integer()) return std::to_string(it->get<int>());
    if (it->is_string()) return it->get<std::string>();
    return {};
}

json parse_gpu(const std::string& text) {
    json a = json::array();
    for (const auto& p : util::split(text, ',')) {
        if (p.empty()) continue;
        try { a.push_back(std::stoi(p)); } catch (...) {}
    }
    return a;
}

std::string filename_of(const std::string& p) {
    return util::u8(util::path(p).filename());
}

std::string stem_of(const std::string& p) {
    return util::u8(util::path(p).stem());
}

std::string parent_of(const std::string& p) {
    return util::u8(util::path(p).parent_path());
}

bool is_strata_root(const std::string& root) {
    return util::is_file(root + "\\serve\\server.py");
}

int64_t file_mtime_epoch(const std::string& p) {
    std::error_code ec;
    auto ft = fs::last_write_time(util::path(p), ec);
    if (ec) return 0;
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return (int64_t)std::chrono::system_clock::to_time_t(sctp);
}

std::string mtime_text(const std::string& p) {
    auto t = (std::time_t)file_mtime_epoch(p);
    if (!t) return {};
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[32]{};
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return buf;
}

void set_bool_or_erase(json& cfg, const char* key, bool value) {
    if (value) cfg[key] = true;
    else cfg.erase(key);
}

}  // namespace

std::optional<std::string> arg_value(const json& args, const std::string& flag) {
    if (!args.is_array()) return std::nullopt;
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i].is_string() && args[i].get<std::string>() == flag) {
            if (args[i + 1].is_string()) return args[i + 1].get<std::string>();
            if (args[i + 1].is_number_integer()) return std::to_string(args[i + 1].get<int>());
            if (args[i + 1].is_number()) return std::to_string(args[i + 1].get<double>());
        }
    }
    return std::nullopt;
}

bool has_flag(const json& args, const std::string& flag) {
    if (!args.is_array()) return false;
    for (const auto& x : args) if (x.is_string() && x.get<std::string>() == flag) return true;
    return false;
}

void set_arg(json& args, const std::string& flag, const std::string& value) {
    if (!args.is_array()) args = json::array();
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i].is_string() && args[i].get<std::string>() == flag) {
            if (i + 1 < args.size()) args[i + 1] = value;
            else args.push_back(value);
            return;
        }
    }
    args.push_back(flag);
    args.push_back(value);
}

void remove_arg(json& args, const std::string& flag) {
    if (!args.is_array()) return;
    for (size_t i = 0; i < args.size();) {
        if (args[i].is_string() && args[i].get<std::string>() == flag) {
            size_t n = (i + 1 < args.size() && args[i + 1].is_string()) ? 2 : 1;
            args.erase(args.begin() + i, args.begin() + i + n);
        } else {
            ++i;
        }
    }
}

void set_flag(json& args, const std::string& flag, bool on) {
    if (!args.is_array()) args = json::array();
    if (on) {
        if (!has_flag(args, flag)) args.push_back(flag);
    } else {
        for (size_t i = 0; i < args.size();) {
            if (args[i].is_string() && args[i].get<std::string>() == flag) args.erase(args.begin() + i);
            else ++i;
        }
    }
}

ConfigStore::ConfigStore(std::string app_dir)
    : app_dir_(std::move(app_dir)),
      launcher_path_(app_dir_ + "\\strata-console.json") {}

json ConfigStore::default_launcher() const {
    return json{
        {"ui_port", 8090},
        {"config_path", ""},
        {"strata_root", ""},
        {"auto_restart", true},
        {"lazy_on_restart", true},
        {"backup_count", 5},
        {"control_token", util::random_hex(24)}
    };
}

void ConfigStore::load_launcher_locked() {
    launcher_ = default_launcher();
    std::string err;
    auto j = util::read_json(launcher_path_, err);
    if (j && j->is_object()) {
        for (auto it = j->begin(); it != j->end(); ++it) launcher_[it.key()] = it.value();
    }
    if (!launcher_.contains("control_token") || !launcher_["control_token"].is_string() ||
        launcher_["control_token"].get<std::string>().empty()) {
        launcher_["control_token"] = util::random_hex(24);
    }
    launcher_["backup_count"] = std::clamp(ival(launcher_, "backup_count", 5), 1, 50);
    launcher_["ui_port"] = std::clamp(ival(launcher_, "ui_port", 8090), 1024, 65535);
}

bool ConfigStore::save_launcher_locked(std::string& err) {
    return util::write_json_atomic(launcher_path_, launcher_, err);
}

std::string ConfigStore::discover_root_locked() const {
    std::string from_launcher = sval(launcher_, "strata_root");
    if (is_strata_root(from_launcher)) return from_launcher;

    std::string appdata = util::get_env_utf8(L"APPDATA");
    if (!appdata.empty()) {
        std::string err;
        auto settings = util::read_json(appdata + "\\Strata\\settings.json", err);
        if (settings && settings->is_object()) {
            auto it = settings->find("installs");
            if (it != settings->end() && it->is_array()) {
                for (const auto& x : *it) {
                    if (x.is_string() && is_strata_root(x.get<std::string>()))
                        return x.get<std::string>();
                }
            }
        }
    }
    std::vector<std::string> candidates;
    candidates.push_back(util::u8(util::path(app_dir_).parent_path() / L"Strata"));
    candidates.push_back(app_dir_ + "\\Strata");
    for (const wchar_t* var : {L"USERPROFILE", L"LOCALAPPDATA", L"ProgramData"}) {
        std::string base = util::get_env_utf8(var);
        if (!base.empty()) candidates.push_back(base + "\\Strata");
    }
    for (const auto& p : candidates) if (is_strata_root(p)) return p;
    return app_dir_;
}

std::string ConfigStore::discover_config_locked() const {
    std::string saved = sval(launcher_, "config_path");
    if (util::is_file(saved)) return saved;
    std::string best;
    int64_t best_time = -1;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(util::path(root_), ec)) {
        if (ec) break;
        if (!e.is_regular_file()) continue;
        std::string name = util::u8(e.path().filename());
        if (name.rfind("strata-", 0) != 0) continue;
        if (name.size() < 5 || name.substr(name.size() - 5) != ".json") continue;
        int64_t t = file_mtime_epoch(util::u8(e.path()));
        if (t > best_time) { best_time = t; best = util::u8(e.path()); }
    }
    return best;
}

void ConfigStore::load_config_locked() {
    if (config_path_.empty()) { cfg_ = json::object(); return; }
    std::string err;
    auto j = util::read_json(config_path_, err);
    if (!j || !j->is_object()) {
        cfg_ = json::object();
        return;
    }
    cfg_ = *j;
    if (!cfg_.contains("args") || !cfg_["args"].is_array()) cfg_["args"] = json::array();
}

bool ConfigStore::init(std::string& err) {
    std::lock_guard<std::mutex> lk(m_);
    std::string launcher_err;
    (void)launcher_err;
    load_launcher_locked();
    root_ = discover_root_locked();
    config_path_ = discover_config_locked();
    if (root_.empty()) root_ = app_dir_;
    load_config_locked();
    if (config_path_.empty()) {
        err = "no strata-*.json found in " + root_;
        return false;
    }
    return true;
}

json ConfigStore::configs_locked() const {
    json out = json::array();
    std::error_code ec;
    if (root_.empty()) return out;
    for (auto& e : fs::directory_iterator(util::path(root_), ec)) {
        if (ec) break;
        if (!e.is_regular_file()) continue;
        std::string name = util::u8(e.path().filename());
        if (name.rfind("strata-", 0) != 0) continue;
        if (name.size() < 5 || name.substr(name.size() - 5) != ".json") continue;
        std::string p = util::u8(e.path());
        std::string model;
        std::string read_err;
        auto j = util::read_json(p, read_err);
        if (j && j->is_object()) model = sval(*j, "model_name");
        out.push_back(json{{"path", p}, {"name", name}, {"model_name", model},
                           {"mtime", file_mtime_epoch(p)}, {"mtime_text", mtime_text(p)}});
    }
    std::sort(out.begin(), out.end(), [](const json& a, const json& b) {
        return a.value("mtime", 0) > b.value("mtime", 0);
    });
    return out;
}

json ConfigStore::configs() const {
    std::lock_guard<std::mutex> lk(m_);
    return configs_locked();
}

std::string ConfigStore::api_key_locked() const {
    auto it = cfg_.find("api_key");
    if (it == cfg_.end() || it->is_null()) return {};
    if (it->is_string()) return it->get<std::string>();
    if (it->is_array()) {
        for (const auto& x : *it) if (x.is_string() && !x.get<std::string>().empty())
            return x.get<std::string>();
    }
    return {};
}

std::string ConfigStore::api_key() const {
    std::lock_guard<std::mutex> lk(m_);
    return api_key_locked();
}

int ConfigStore::model_port_locked() const {
    return ival(cfg_, "port", 8080);
}

int ConfigStore::model_port() const {
    std::lock_guard<std::mutex> lk(m_);
    return model_port_locked();
}

int ConfigStore::ui_port() const {
    std::lock_guard<std::mutex> lk(m_);
    return ival(launcher_, "ui_port", 8090);
}

std::string ConfigStore::log_path_locked() const {
    std::string p = sval(cfg_, "log");
    if (!p.empty()) return p;
    if (!root_.empty()) return root_ + "\\strata-console-server.log";
    return {};
}

std::string ConfigStore::log_path() const {
    std::lock_guard<std::mutex> lk(m_);
    return log_path_locked();
}

std::string ConfigStore::root() const {
    std::lock_guard<std::mutex> lk(m_);
    return root_;
}

std::string ConfigStore::config_path() const {
    std::lock_guard<std::mutex> lk(m_);
    return config_path_;
}

std::string ConfigStore::control_token() const {
    std::lock_guard<std::mutex> lk(m_);
    return sval(launcher_, "control_token");
}

bool ConfigStore::vision_enabled_locked() const {
    auto it = cfg_.find("vision");
    if (it != cfg_.end() && it->is_object() && !it->empty()) return true;
    return has_flag(cfg_.value("args", json::array()), "--vision");
}

std::string ConfigStore::start_mode_locked() const {
    return bval(cfg_, "lazy_load", false) ? "lazy" : "preload";
}

json ConfigStore::view_locked() const {
    json j = json::object();
    j["config_path"] = config_path_;
    j["config_name"] = config_path_.empty() ? "" : filename_of(config_path_);
    j["strata_root"] = root_;
    j["model_name"] = sval(cfg_, "model_name");
    j["exe"] = sval(cfg_, "exe");
    j["tokenizer"] = sval(cfg_, "tokenizer");
    j["log"] = log_path_locked();
    j["host"] = sval(cfg_, "host", "127.0.0.1");
    j["port"] = model_port_locked();
    {
        std::string key = api_key_locked();
        j["api_key_set"] = !key.empty();
        j["api_key_prefix"] = key.empty() ? "" :
            (key.size() > 14 ? key.substr(0, 14) + "…" : key.substr(0, std::min<size_t>(4, key.size())) + "…");
    }
    j["cors_origins"] = list_text(cfg_.value("cors_origins", json()));
    j["trusted_origins"] = list_text(cfg_.value("trusted_origins", json()));
    j["allowed_hosts"] = list_text(cfg_.value("allowed_hosts", json()));
    j["idle_unload_min"] = cfg_.contains("idle_unload_s")
        ? dval(cfg_, "idle_unload_s", 0.0) / 60.0 : 60.0;
    j["open_browser"] = bval(cfg_, "open_browser", false);
    j["prevent_sleep"] = bval(cfg_, "prevent_sleep", false);
    j["api_monitor"] = bval(cfg_, "api_monitor", false);
    j["start_mode"] = start_mode_locked();
    j["gpu"] = gpu_text(cfg_);
    j["reasoning_effort"] = sval(cfg_, "reasoning_effort");
    {
        auto s = cfg_.value("sampling", json::object());
        if (!s.is_object()) s = json::object();
        j["temperature"] = s.contains("temperature") ? s["temperature"] : json();
        j["top_p"] = s.contains("top_p") ? s["top_p"] : json();
        j["top_k"] = s.contains("top_k") ? s["top_k"] : json();
        j["seed"] = s.contains("seed") ? s["seed"] : json();
    }

    auto args = cfg_.value("args", json::array());
    j["native"] = arg_value(args, "--native").value_or("");
    j["ple"] = arg_value(args, "--ple-gguf").value_or("");
    j["pack"] = arg_value(args, "--pack").value_or("");
    j["expert_profile"] = arg_value(args, "--expert-profile").value_or("");
    auto arg_int = [&](const char* flag, int fallback = 0) {
        try { return std::stoi(arg_value(args, flag).value_or(std::to_string(fallback))); }
        catch (...) { return fallback; }
    };
    j["context"] = arg_int("--max-context", 0);
    j["kv"] = arg_value(args, "--kv").value_or("int8");
    j["spec"] = arg_value(args, "--spec").value_or("");
    j["spec_min_p"] = arg_value(args, "--spec-min-p").value_or("");
    j["prefill"] = arg_value(args, "--prefill").value_or("");
    j["expert_cache"] = arg_value(args, "--expert-cache").value_or("");
    j["kv_resident"] = arg_value(args, "--kv-resident").value_or("");
    j["vram_reserve"] = arg_int("--vram-reserve-mib", 1024);

    std::string low_ram = has_flag(args, "--mmap-experts") ? "mmap" :
                          has_flag(args, "--resident-experts") ? "resident" : "off";
    j["low_ram"] = low_ram;
    j["vision"] = vision_enabled_locked();
    if (cfg_.contains("vision") && cfg_["vision"].is_object()) {
        j["vision_exe"] = sval(cfg_["vision"], "exe");
        j["vision_mmproj"] = sval(cfg_["vision"], "mmproj");
        j["vision_gpu"] = bval(cfg_["vision"], "gpu", false);
        j["vision_max_tokens"] = ival(cfg_["vision"], "max_tokens", 768);
        j["vision_threads"] = ival(cfg_["vision"], "threads", 8);
    } else {
        j["vision_exe"] = "";
        j["vision_mmproj"] = "";
        j["vision_gpu"] = false;
        j["vision_max_tokens"] = 768;
        j["vision_threads"] = 8;
    }

    json lan_urls = json::array();
    for (const auto& ip : util::local_ipv4s()) lan_urls.push_back("http://" + ip);
    j["lan_urls"] = lan_urls;

    j["auto_restart"] = bval(launcher_, "auto_restart", true);
    j["lazy_on_restart"] = bval(launcher_, "lazy_on_restart", true);
    j["backup_count"] = ival(launcher_, "backup_count", 5);
    j["ui_port"] = ival(launcher_, "ui_port", 8090);

    json warnings = json::array();
    if (j["host"].get<std::string>() != "127.0.0.1" &&
        j["host"].get<std::string>() != "localhost" &&
        j["host"].get<std::string>() != "::1" &&
        !j["api_key_set"].get<bool>()) {
        warnings.push_back("模型 API 监听在非本机地址，但还没有设置 API Key。");
    }
    if (j["vision"].get<bool>() &&
        (!util::is_file(j["vision_exe"].get<std::string>()) ||
         !util::is_file(j["vision_mmproj"].get<std::string>()))) {
        warnings.push_back("图片已开启，但视觉编码器或 mmproj 不存在。");
    }
    j["warnings"] = warnings;
    return j;
}

json ConfigStore::view() const {
    std::lock_guard<std::mutex> lk(m_);
    return view_locked();
}

json ConfigStore::launcher_public() const {
    std::lock_guard<std::mutex> lk(m_);
    return json{{"ui_port", ival(launcher_, "ui_port", 8090)}};
}

bool ConfigStore::select_config(const std::string& path, std::string& err) {
    std::lock_guard<std::mutex> lk(m_);
    if (!util::is_file(path)) { err = "配置文件不存在: " + path; return false; }
    std::string read_err;
    auto j = util::read_json(path, read_err);
    if (!j || !j->is_object()) { err = "配置文件不是有效的 JSON: " + read_err; return false; }
    config_path_ = path;
    cfg_ = *j;
    if (!cfg_.contains("args") || !cfg_["args"].is_array()) cfg_["args"] = json::array();
    launcher_["config_path"] = config_path_;
    launcher_["strata_root"] = root_;
    std::string save_err;
    if (!save_launcher_locked(save_err)) { err = save_err; return false; }
    return true;
}

std::string ConfigStore::backup_dir_locked() const {
    if (root_.empty() || config_path_.empty()) return {};
    return root_ + "\\config-backups\\" + stem_of(config_path_);
}

bool ConfigStore::create_backup_locked(std::string& err) {
    if (config_path_.empty() || !util::is_file(config_path_)) return true;
    std::string dir = backup_dir_locked();
    std::error_code ec;
    fs::create_directories(util::path(dir), ec);
    if (ec) { err = "无法创建备份目录: " + dir; return false; }
    std::string base = stem_of(config_path_) + "." + util::timestamp_compact();
    std::string target = dir + "\\" + base + ".bak";
    for (int i = 1; util::is_file(target) && i < 100; ++i) {
        target = dir + "\\" + base + "-" + std::to_string(i) + ".bak";
    }
    fs::copy_file(util::path(config_path_), util::path(target),
                  fs::copy_options::overwrite_existing, ec);
    if (ec) { err = "备份失败: " + ec.message(); return false; }
    prune_backups_locked();
    return true;
}

void ConfigStore::prune_backups_locked() {
    std::string dir = backup_dir_locked();
    if (dir.empty()) return;
    int keep = std::clamp(ival(launcher_, "backup_count", 5), 1, 50);
    std::vector<fs::directory_entry> items;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(util::path(dir), ec)) {
        if (ec) break;
        if (e.is_regular_file() &&
            e.path().extension() == L".bak") items.push_back(e);
    }
    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
        std::error_code e1, e2;
        return fs::last_write_time(a.path(), e1) > fs::last_write_time(b.path(), e2);
    });
    for (size_t i = (size_t)keep; i < items.size(); ++i) {
        std::error_code de;
        fs::remove(items[i].path(), de);
    }
}

bool ConfigStore::save_config_locked(std::string& err) {
    if (config_path_.empty()) { err = "没有选中的配置文件"; return false; }
    if (!create_backup_locked(err)) return false;
    return util::write_json_atomic(config_path_, cfg_, err);
}

json ConfigStore::backups_locked() const {
    json out = json::array();
    std::string dir = backup_dir_locked();
    if (dir.empty()) return out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(util::path(dir), ec)) {
        if (ec) break;
        if (!e.is_regular_file() || e.path().extension() != L".bak") continue;
        std::string p = util::u8(e.path());
        std::string model;
        int context = 0;
        std::string kv;
        std::string read_err;
        auto j = util::read_json(p, read_err);
        if (j && j->is_object()) {
            model = sval(*j, "model_name");
            auto args = j->value("args", json::array());
            try { context = std::stoi(arg_value(args, "--max-context").value_or("0")); } catch (...) {}
            kv = arg_value(args, "--kv").value_or("");
        }
        out.push_back(json{{"name", util::u8(e.path().filename())},
                           {"path", p},
                           {"mtime", file_mtime_epoch(p)},
                           {"mtime_text", mtime_text(p)},
                           {"size", util::file_size_str(p)},
                           {"model_name", model},
                           {"context", context},
                           {"kv", kv}});
    }
    std::sort(out.begin(), out.end(), [](const json& a, const json& b) {
        return a.value("mtime", 0) > b.value("mtime", 0);
    });
    return out;
}

json ConfigStore::backups() const {
    std::lock_guard<std::mutex> lk(m_);
    return backups_locked();
}

bool ConfigStore::restore_backup(const std::string& path, std::string& err) {
    std::lock_guard<std::mutex> lk(m_);
    if (!util::is_file(path)) { err = "备份不存在: " + path; return false; }
    std::string dir = backup_dir_locked();
    std::string canonical_dir = util::u8(fs::weakly_canonical(util::path(dir)));
    std::string canonical_file = util::u8(fs::weakly_canonical(util::path(path)));
    if (canonical_dir.empty() ||
        canonical_file.rfind(canonical_dir + "\\", 0) != 0) {
        err = "只能恢复当前配置的备份";
        return false;
    }
    std::string read_err;
    auto j = util::read_json(path, read_err);
    if (!j || !j->is_object()) { err = "备份不是有效 JSON: " + read_err; return false; }
    if (!create_backup_locked(err)) return false;
    std::error_code ec;
    fs::copy_file(util::path(path), util::path(config_path_),
                  fs::copy_options::overwrite_existing, ec);
    if (ec) { err = "恢复失败: " + ec.message(); return false; }
    cfg_ = *j;
    if (!cfg_.contains("args") || !cfg_["args"].is_array()) cfg_["args"] = json::array();
    return true;
}

bool ConfigStore::apply(const json& form, std::string& err) {
    std::lock_guard<std::mutex> lk(m_);
    if (config_path_.empty()) { err = "没有选中的配置文件"; return false; }
    if (!form.is_object()) { err = "参数格式错误"; return false; }

    auto str = [&](const char* k, const std::string& def = "") {
        return form.contains(k) && form[k].is_string() ? form[k].get<std::string>() : def;
    };
    auto num = [&](const char* k, double def = 0) {
        if (!form.contains(k)) return def;
        try {
            if (form[k].is_number()) return form[k].get<double>();
            if (form[k].is_string()) return std::stod(form[k].get<std::string>());
        } catch (...) {}
        return def;
    };
    auto flag = [&](const char* k, bool def = false) {
        return form.contains(k) && form[k].is_boolean() ? form[k].get<bool>() : def;
    };

    json args = cfg_.value("args", json::array());
    if (!args.is_array()) args = json::array();

    if (form.contains("model_name")) cfg_["model_name"] = str("model_name");
    if (form.contains("exe") && !str("exe").empty()) cfg_["exe"] = str("exe");
    if (form.contains("tokenizer")) cfg_["tokenizer"] = str("tokenizer");
    if (form.contains("log")) cfg_["log"] = str("log");
    if (form.contains("host") && !str("host").empty()) cfg_["host"] = str("host");
    if (form.contains("port")) {
        int port = (int)num("port", 8080);
        if (port < 1 || port > 65535) { err = "端口必须在 1-65535 之间"; return false; }
        cfg_["port"] = port;
    }

    if (form.contains("native")) {
        std::string v = str("native");
        if (v.empty()) remove_arg(args, "--native"); else set_arg(args, "--native", v);
    }
    if (form.contains("ple")) {
        std::string v = str("ple");
        if (v.empty()) remove_arg(args, "--ple-gguf"); else set_arg(args, "--ple-gguf", v);
    }
    if (form.contains("pack")) {
        std::string v = str("pack");
        if (v.empty()) remove_arg(args, "--pack"); else set_arg(args, "--pack", v);
    }
    if (form.contains("expert_profile")) {
        std::string v = str("expert_profile");
        if (v.empty()) remove_arg(args, "--expert-profile");
        else set_arg(args, "--expert-profile", v);
    }

    if (form.contains("context")) {
        int ctx = (int)num("context", 0);
        if (ctx <= 0) { err = "上下文长度必须大于 0"; return false; }
        set_arg(args, "--max-context", std::to_string(ctx));
    }
    if (form.contains("kv")) {
        std::string kv = str("kv", "int8");
        if (kv != "fp16" && kv != "int8" && kv != "q4_0" && kv != "k8v4") {
            err = "KV 类型只支持 fp16 / int8 / q4_0 / k8v4";
            return false;
        }
        set_arg(args, "--kv", kv);
    }

    if (form.contains("low_ram")) {
        set_flag(args, "--mmap-experts", false);
        set_flag(args, "--resident-experts", false);
        std::string v = str("low_ram");
        if (v == "mmap") set_flag(args, "--mmap-experts", true);
        else if (v == "resident") set_flag(args, "--resident-experts", true);
    }

    auto set_optional = [&](const char* key, const char* flag_name) {
        if (!form.contains(key)) return;
        std::string v = str(key);
        if (v.empty()) remove_arg(args, flag_name); else set_arg(args, flag_name, v);
    };
    set_optional("spec", "--spec");
    set_optional("spec_min_p", "--spec-min-p");
    set_optional("prefill", "--prefill");
    set_optional("expert_cache", "--expert-cache");
    set_optional("kv_resident", "--kv-resident");
    if (form.contains("vram_reserve")) {
        int v = (int)num("vram_reserve", 0);
        if (v <= 0) remove_arg(args, "--vram-reserve-mib");
        else set_arg(args, "--vram-reserve-mib", std::to_string(v));
    }

    if (form.contains("vision")) {
        bool on = flag("vision", false);
        if (on) {
            std::string exe = str("vision_exe");
            std::string mmproj = str("vision_mmproj");
            if (exe.empty() && cfg_.contains("vision") && cfg_["vision"].is_object())
                exe = sval(cfg_["vision"], "exe");
            if (mmproj.empty() && cfg_.contains("vision") && cfg_["vision"].is_object())
                mmproj = sval(cfg_["vision"], "mmproj");
            if (exe.empty()) exe = root_ + "\\engine\\strata-vision.exe";
            if (mmproj.empty()) {
                std::string native = arg_value(args, "--native").value_or("");
                std::string dir = parent_of(native);
                std::error_code ec;
                if (!dir.empty()) {
                    for (auto& e : fs::directory_iterator(util::path(dir), ec)) {
                        if (ec) break;
                        std::string n = util::u8(e.path().filename());
                        if (n.rfind("mmproj", 0) == 0 && e.path().extension() == L".gguf") {
                            mmproj = util::u8(e.path());
                            break;
                        }
                    }
                }
            }
            if (!util::is_file(exe)) { err = "视觉编码器不存在: " + exe; return false; }
            if (!util::is_file(mmproj)) { err = "视觉 mmproj 不存在: " + mmproj; return false; }
            json v = cfg_.contains("vision") && cfg_["vision"].is_object()
                         ? cfg_["vision"] : json::object();
            v["exe"] = exe;
            v["mmproj"] = mmproj;
            v["model"] = arg_value(args, "--native").value_or(sval(v, "model"));
            v["gpu"] = flag("vision_gpu", false);
            int max_tokens = form.contains("vision_max_tokens")
                ? (int)num("vision_max_tokens", ival(v, "max_tokens", 768))
                : ival(v, "max_tokens", 768);
            int threads = form.contains("vision_threads")
                ? (int)num("vision_threads", ival(v, "threads", 8))
                : ival(v, "threads", 8);
            if (max_tokens < 64 || max_tokens > 4096) {
                err = "视觉最大 tokens 必须在 64-4096 之间"; return false;
            }
            if (threads < 1 || threads > 128) {
                err = "视觉线程数必须在 1-128 之间"; return false;
            }
            v["max_tokens"] = max_tokens;
            v["threads"] = threads;
            cfg_["vision"] = v;
            set_flag(args, "--vision", true);
        } else {
            cfg_.erase("vision");
            set_flag(args, "--vision", false);
        }
    }

    if (form.contains("idle_unload_min")) {
        double minutes = std::max(0.0, num("idle_unload_min", 60));
        int seconds = (int)std::llround(minutes * 60.0);
        cfg_["idle_unload_s"] = seconds;
    }

    if (form.contains("gpu")) {
        std::string g = str("gpu");
        if (g.empty()) { cfg_.erase("gpu"); cfg_.erase("gpus"); }
        else cfg_["gpu"] = parse_gpu(g);
    }

    if (form.contains("api_key_clear") && flag("api_key_clear", false)) {
        cfg_.erase("api_key");
    } else if (form.contains("api_key")) {
        std::string key = str("api_key");
        if (!key.empty()) cfg_["api_key"] = key;
    }

    if (form.contains("cors_origins")) {
        auto a = text_list(str("cors_origins"));
        if (a.empty()) cfg_.erase("cors_origins"); else cfg_["cors_origins"] = a;
    }
    if (form.contains("trusted_origins")) {
        auto a = text_list(str("trusted_origins"));
        if (a.empty()) cfg_.erase("trusted_origins"); else cfg_["trusted_origins"] = a;
    }
    if (form.contains("allowed_hosts")) {
        auto a = text_list(str("allowed_hosts"));
        if (a.empty()) cfg_.erase("allowed_hosts"); else cfg_["allowed_hosts"] = a;
    }

    if (form.contains("open_browser")) set_bool_or_erase(cfg_, "open_browser", flag("open_browser", false));
    if (form.contains("prevent_sleep")) set_bool_or_erase(cfg_, "prevent_sleep", flag("prevent_sleep", false));
    if (form.contains("api_monitor")) set_bool_or_erase(cfg_, "api_monitor", flag("api_monitor", false));
    if (form.contains("start_mode")) {
        bool lazy = str("start_mode") == "lazy";
        set_bool_or_erase(cfg_, "lazy_load", lazy);
    }

    if (form.contains("reasoning_effort")) {
        std::string v = str("reasoning_effort");
        if (v.empty() || v == "default") {
            cfg_.erase("reasoning_effort");
        } else if (v == "none" || v == "low" || v == "medium" || v == "high") {
            cfg_["reasoning_effort"] = v;
        } else {
            err = "默认思考强度只支持 none / low / medium / high";
            return false;
        }
    }

    {
        json sampling = cfg_.value("sampling", json::object());
        if (!sampling.is_object()) sampling = json::object();
        auto sampling_text = [&](const char* key) {
            if (!form.contains(key)) return std::optional<std::string>{};
            if (form[key].is_null()) return std::optional<std::string>{std::string()};
            if (form[key].is_string()) return std::optional<std::string>{form[key].get<std::string>()};
            if (form[key].is_number()) return std::optional<std::string>{std::to_string(form[key].get<double>())};
            return std::optional<std::string>{std::string()};
        };
        auto apply_number = [&](const char* key, double min, double max, bool integer) -> bool {
            auto v = sampling_text(key);
            if (!v) return true;
            std::string s = util::trim(*v);
            if (s.empty()) { sampling.erase(key); return true; }
            try {
                double d = std::stod(s);
                if (d < min || d > max || (integer && std::floor(d) != d)) {
                    err = std::string("采样参数 ") + key + " 超出范围";
                    return false;
                }
                sampling[key] = integer ? json((long long)d) : json(d);
                return true;
            } catch (...) {
                err = std::string("采样参数 ") + key + " 不是数字";
                return false;
            }
        };
        if (!apply_number("temperature", 0, 2, false)) return false;
        if (!apply_number("top_p", 0.000001, 1, false)) return false;
        if (!apply_number("top_k", 1, 64, true)) return false;
        if (!apply_number("seed", 1, 9.0e18, true)) return false;
        if (sampling.empty()) cfg_.erase("sampling");
        else cfg_["sampling"] = sampling;
    }

    if (form.contains("auto_restart")) launcher_["auto_restart"] = flag("auto_restart", false);
    if (form.contains("lazy_on_restart")) launcher_["lazy_on_restart"] = flag("lazy_on_restart", true);
    if (form.contains("backup_count")) {
        launcher_["backup_count"] = std::clamp((int)num("backup_count", 5), 1, 50);
    }
    launcher_["config_path"] = config_path_;
    launcher_["strata_root"] = root_;

    cfg_["args"] = args;
    if (!save_config_locked(err)) return false;
    std::string launcher_err;
    if (!save_launcher_locked(launcher_err)) { err = launcher_err; return false; }
    return true;
}

bool ConfigStore::auto_restart() const {
    std::lock_guard<std::mutex> lk(m_);
    return bval(launcher_, "auto_restart", true);
}

bool ConfigStore::lazy_on_restart() const {
    std::lock_guard<std::mutex> lk(m_);
    return bval(launcher_, "lazy_on_restart", true);
}

std::wstring ConfigStore::build_command_locked(bool force_lazy) const {
    std::string python = root_ + "\\.venv\\Scripts\\python.exe";
    if (!util::is_file(python)) python = "python.exe";
    std::string server = root_ + "\\serve\\server.py";
    int port = model_port_locked();
    std::string host = sval(cfg_, "host", "127.0.0.1");
    if (host.empty()) host = "127.0.0.1";

    std::wstring cmd = util::quote_arg(util::widen(python)) + L" -u " +
                       util::quote_arg(util::widen(server)) +
                       L" --engine strata --config " + util::quote_arg(util::widen(config_path_)) +
                       L" --port " + std::to_wstring(port) +
                       L" --host " + util::quote_arg(util::widen(host));
    bool lazy = force_lazy || bval(cfg_, "lazy_load", false);
    if (lazy) cmd += L" --lazy";
    if (bval(cfg_, "open_browser", false)) cmd += L" --open";
    return cmd;
}

std::wstring ConfigStore::build_command(bool force_lazy) const {
    std::lock_guard<std::mutex> lk(m_);
    return build_command_locked(force_lazy);
}

bool ConfigStore::start_preflight(std::string& err) const {
    std::lock_guard<std::mutex> lk(m_);
    if (config_path_.empty()) { err = "没有选中的配置文件"; return false; }
    if (!util::is_file(config_path_)) { err = "配置文件不存在: " + config_path_; return false; }
    std::string server = root_ + "\\serve\\server.py";
    if (!util::is_file(server)) { err = "找不到 serve/server.py: " + server; return false; }
    std::string python = root_ + "\\.venv\\Scripts\\python.exe";
    if (!util::is_file(python)) { err = "找不到 Strata Python: " + python; return false; }
    std::string exe = sval(cfg_, "exe");
    if (!util::is_file(exe)) { err = "引擎 exe 不存在: " + exe; return false; }
    auto args = cfg_.value("args", json::array());
    std::string pack = arg_value(args, "--pack").value_or("");
    if (!pack.empty() && !util::is_dir(pack)) { err = "Pack 目录不存在: " + pack; return false; }
    std::string native = arg_value(args, "--native").value_or("");
    if (!native.empty() && !util::is_file(native)) { err = "模型首分片不存在: " + native; return false; }
    std::string tokenizer = sval(cfg_, "tokenizer");
    if (!tokenizer.empty() && !util::is_file(tokenizer + "\\vocab.json")) {
        err = "找不到 tokenizer/vocab.json: " + tokenizer; return false;
    }
    if (vision_enabled_locked()) {
        std::string vexe = cfg_.contains("vision") ? sval(cfg_["vision"], "exe") : "";
        std::string vmm = cfg_.contains("vision") ? sval(cfg_["vision"], "mmproj") : "";
        if (!util::is_file(vexe)) { err = "视觉编码器不存在: " + vexe; return false; }
        if (!util::is_file(vmm)) { err = "视觉 mmproj 不存在: " + vmm; return false; }
    }
    int port = model_port_locked();
    if (port < 1 || port > 65535) { err = "端口无效"; return false; }
    if (util::tcp_port_open(port)) { err = "端口 " + std::to_string(port) + " 已被占用"; return false; }
    if (!util::is_loopback_host(sval(cfg_, "host", "127.0.0.1")) && api_key_locked().empty()) {
        err = "监听非本机地址时必须设置 API Key"; return false;
    }
    return true;
}

json ConfigStore::command_preview() const {
    std::lock_guard<std::mutex> lk(m_);
    std::wstring cmd = build_command_locked(false);
    return json{{"server", util::narrow(cmd)},
                {"config", config_path_},
                {"engine_args", cfg_.value("args", json::array())},
                {"log", log_path_locked()}};
}


