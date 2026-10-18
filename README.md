# Strata Console

本地 Web 控制台，用于启动/停止 Strata、编辑运行参数、查看官方 Monitor 指标和日志。界面风格与 Strata 官方网页一致，默认只监听 `127.0.0.1:8090`。

## 快速开始

```text
双击 strata-console.exe            # 启动控制台并打开浏览器
start-daemon.bat                   # 只启动后台服务，不打开浏览器
stop-console.bat                   # 停止控制台（同时停止它启动的模型）
```

首次启动会：

- 自动从 Strata 安装目录（或 `%APPDATA%\Strata\settings.json` 中记录的安装）读取 `strata-*.json`；
- 读取模型/Pack/Tokenizer/引擎路径；
- 生成 `strata-console.json`（控制台自己的设置）；
- 使用官方风格界面，默认不打开 Strata 的 8080 页面。

## 端口

| 端口 | 用途 |
|---|---|
| 8090 | Strata Console 控制台（默认仅本机） |
| 8080 | Strata 模型 API / 官方页面 |

控制台与模型 API 是两套独立配置。控制台默认只显示 API Key 前缀；复制时由本机后台按需读取，状态由后台进程代理。

## 功能

- 启动、停止、重启、加载、卸载模型；
- 配置区按“基本 / 推理 / 网络 / 配置管理”分页，减少滚动；
- 自动读取配置档，编辑上下文、KV、图片、空闲卸载、低内存模式、GPU、投机解码等；
- 采样与思考：默认思考强度（reasoning_effort）、Temperature、Top P、Top K、Seed（留空使用模型默认）；
- 图片可选 CPU / GPU 编码器（需要视觉组件）；
- 视觉编码器可配置最大图片 tokens（默认 768）和 CPU 线程数（默认 8）；
- Prefill 分块大小、Expert Cache 专家数等高级性能参数；
- 官方 Monitor 指标：GPU、VRAM、温度、功耗、CPU、RAM、磁盘、PCIe、当前请求、最近请求、专家缓存；
- 连接信息：一键复制 OpenAI 兼容 API Base、Chat Completions、Anthropic Messages 和模型名；
- API Key 支持生成 `sk-strata-*`、前缀掩码显示和多次复制；
- 局域网访问默认关闭；CORS 默认关闭，可独立于局域网模式配置（支持反向代理/隧道）；
- 顶部“退出控制台”明确退出后台服务，可选择模型继续运行或一并停止；
- 命令行 Tab：实时跟随 server/engine 日志；
- 请求 Tab：默认显示 `/metrics` 元数据；开启 `api_monitor` 后可打开官方 `/api-monitor` 查看请求内容；
- 自动配置：调用 Strata 自己的 `tools/strata_mcp.py` 读取硬件与推荐规则；
- 配置写回官方 `strata-*.json`，每次保存生成带时间戳的 `.bak`，默认保留 5 个；
- 中断后自动重启（默认开启）；自动重启默认加 `--lazy`，首个请求才加载模型；
- 默认空闲 60 分钟卸载模型，可在界面按分钟修改；默认保留 1024 MiB VRAM，降低显存不足导致的崩溃风险；
- 参数名悬停显示说明；
- 图片开关保留；当前缺少 `strata-vision.exe` 时显示不可用，安装后可启用；
- 深色/浅色主题。

## 构建

需要 Visual Studio 2022 BuildTools（x64）和 Windows SDK。

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

产物：`strata-console.exe`。依赖已放在 `third_party/`（cpp-httplib、nlohmann/json），无运行时依赖。

## 图片输入（本机当前配置）

- 编码器：`<Strata 根目录>\engine\strata-vision.exe`（CPU 构建）
- mmproj：`<模型目录>\mmproj-<模型名>-Q8_0.gguf`
- 运行位置：CPU（`vision.gpu = false`）
- 最大图片 tokens：768
- CPU 线程：8
- 已通过截图请求验证，`/health` 返回 `images: true`

选择 CPU 的原因：当前 16 GB 显卡加载 128K/int8 后只剩约 0.8 GB 显存，GPU 编码器需要约 1.4 GB；CPU 编码器不占显存，代价是单张图片约数秒。若要改用 GPU，需要重新编译带 CUDA 的 `strata-vision`，把 `vision.gpu` 设为 `true`，并适当降低上下文、专家缓存或 VRAM 保留。

## 配置与备份

- 控制台设置：`strata-console.json`（与 exe 同目录）
- Strata 运行配置：`<Strata 根目录>\strata-*.json`
- 备份：`<Strata 根目录>\config-backups\<配置名>\<配置名>.YYYYMMDD-HHMMSS.bak`
- 保存前自动备份；默认保留 5 个，可在界面修改为 1–50。

## 安全说明

- 控制台默认只绑定 `127.0.0.1:8090`，不对局域网开放。
- 所有修改请求要求 JSON + `X-CSRF-Token`，并校验 Host/Origin；浏览器同源脚本才能取得 token。
- 没有登录页；这是本机控制台。如果未来需要公网/远程访问，应增加管理员登录、TLS/Cloudflare Access 等，见 `design-notes/WebUI控制台方案.md`（该目录已加入 `.gitignore`）。
- `api/browse`、`api/open`、`api/quit` 等高权限操作都要求 CSRF token。
- API Key 只在服务端配置文件中；界面默认只显示前缀（如 `sk-strata-…`），复制时通过本机受 CSRF 保护的接口读取，不写入日志。

## 限制与后续

- 公网访问配置界面暂未实现（按确认结果写入后续计划）。
- 请求内容详情目前通过官方 `/api-monitor` 查看；控制台请求 Tab 只显示元数据。
- 自动配置使用 Strata 的推荐规则，但不会更换当前模型或修改路径。
- 控制台退出后模型可以继续运行（“仅退出控制台”）；此时自动重启监督停止，下次启动会识别为外部服务。

## 分发前检查

- 不要提交 `strata-console.json`：它包含本机路径和 `control_token`，已列入 `.gitignore`。
- 不要提交 `build/`、`strata-console.exe`、`strata-console-server.log` 或本机 `config-backups/`；可执行文件建议放到 GitHub Releases。
- `design-notes/` 是本地调研/设计笔记与截图，已加入 `.gitignore`，不会进入 GitHub；里面的示例路径仅用于说明，不是运行时代码。
- 源码中没有写死本机 Strata 路径：启动器会从 `%APPDATA%\Strata\settings.json`、启动器同级的 `Strata` 目录或用户目录下的 `Strata` 目录查找；`build.ps1` 通过 `vswhere` 或 `STRATA_VCVARS` 查找 Visual Studio。
- 项目使用 MIT License，见 `LICENSE`；第三方组件声明见 `THIRD_PARTY_NOTICES.md`。

## License

MIT License，见 `LICENSE`。

## 第三方

- Strata: MIT License, Copyright (c) 2026 Niko1221 and the Strata contributors.
- Outfit font: SIL Open Font License 1.1（`ui/fonts/OFL.txt`）。
- cpp-httplib: MIT License.
- nlohmann/json: MIT License.




