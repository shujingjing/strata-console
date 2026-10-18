#pragma once
#include <mutex>
#include <string>

#include "util.h"

class ConfigStore {
public:
    explicit ConfigStore(std::string app_dir);

    bool init(std::string& err);

    json configs() const;
    json view() const;
    json launcher_public() const;

    bool select_config(const std::string& path, std::string& err);
    bool apply(const json& form, std::string& err);

    bool start_preflight(std::string& err) const;
    json command_preview() const;
    std::wstring build_command(bool force_lazy) const;

    json backups() const;
    bool restore_backup(const std::string& path, std::string& err);

    std::string api_key() const;
    std::string control_token() const;
    int model_port() const;
    int ui_port() const;
    std::string log_path() const;
    std::string root() const;
    std::string config_path() const;
    bool auto_restart() const;
    bool lazy_on_restart() const;

private:
    std::string app_dir_;
    std::string launcher_path_;
    mutable std::mutex m_;
    json launcher_ = json::object();
    json cfg_ = json::object();
    std::string config_path_;
    std::string root_;

    json default_launcher() const;
    void load_launcher_locked();
    bool save_launcher_locked(std::string& err);
    std::string discover_root_locked() const;
    std::string discover_config_locked() const;
    void load_config_locked();
    json view_locked() const;
    json configs_locked() const;
    json backups_locked() const;
    std::string backup_dir_locked() const;
    bool create_backup_locked(std::string& err);
    void prune_backups_locked();
    bool save_config_locked(std::string& err);
    std::string api_key_locked() const;
    int model_port_locked() const;
    std::string log_path_locked() const;
    bool vision_enabled_locked() const;
    std::string start_mode_locked() const;
    std::wstring build_command_locked(bool force_lazy) const;
};

// --- small JSON arg helpers (shared with the process manager) ---
std::optional<std::string> arg_value(const json& args, const std::string& flag);
bool has_flag(const json& args, const std::string& flag);
void set_arg(json& args, const std::string& flag, const std::string& value);
void remove_arg(json& args, const std::string& flag);
void set_flag(json& args, const std::string& flag, bool on);
