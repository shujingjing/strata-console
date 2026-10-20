"use strict";

const $ = (id) => document.getElementById(id);
const state = {
  csrf: "",
  payload: null,
  live: null,
  dirty: false,
  original: {},
  tab: "monitor",
  logPaused: false,
  logLines: [],
  logPath: "",
  clearKey: false,
  apiKeySet: false,
  lastError: "",
  pollTimer: null,
};

const stateLabels = {
  stopped: "未启动",
  starting: "启动中",
  loading: "加载中",
  running: "运行中",
  unloaded: "未加载",
  stopping: "停止中",
  restarting: "重启中",
  error: "错误",
  external: "外部服务",
};

const liveLabels = {
  idle: "空闲",
  reading: "读取提示词",
  generating: "生成中",
  unloaded: "未加载",
  queued: "排队中",
};

function esc(v) {
  return String(v ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
}

function fmt(n, digits = 0) {
  if (n === null || n === undefined || Number.isNaN(Number(n))) return "—";
  return Number(n).toLocaleString("zh-CN", { maximumFractionDigits: digits, minimumFractionDigits: 0 });
}

function gb(v, digits = 1) {
  if (v === null || v === undefined) return "—";
  return fmt(Number(v) / 1073741824, digits);
}

function pct(part, total) {
  if (!total) return 0;
  return Math.max(0, Math.min(100, (Number(part) / Number(total)) * 100));
}

function setHtml(id, html) {
  const el = $(id);
  if (el) el.innerHTML = html;
}

function setText(id, text) {
  const el = $(id);
  if (el) el.textContent = text ?? "";
}

async function api(path, opts = {}) {
  const method = (opts.method || "GET").toUpperCase();
  const headers = { Accept: "application/json", ...(opts.headers || {}) };
  if (method !== "GET") {
    headers["Content-Type"] = "application/json";
    headers["X-CSRF-Token"] = state.csrf;
  }
  const res = await fetch(path, { ...opts, method, headers, cache: "no-store" });
  let body = null;
  try { body = await res.json(); } catch (e) { /* not JSON */ }
  if (!res.ok && res.status === 403 && body?.error?.includes("CSRF") && path !== "/api/session") {
    const s = await fetch("/api/session", { cache: "no-store" }).then((r) => r.json());
    state.csrf = s.token;
    return api(path, opts);
  }
  if (!res.ok) {
    const msg = body && (body.error || body.message) ? (body.error || body.message) : `HTTP ${res.status}`;
    throw new Error(msg);
  }
  return body;
}

const post = (path, data = {}) => api(path, { method: "POST", body: JSON.stringify(data) });

function toast(message, type = "ok", ms = 4500) {
  const box = document.createElement("div");
  box.className = `toast ${type}`;
  box.textContent = message;
  $("toasts").appendChild(box);
  setTimeout(() => box.remove(), ms);
}

function setToggle(id, on) {
  const el = $(id);
  if (el) el.setAttribute("aria-checked", on ? "true" : "false");
}

function isOn(id) {
  return $(id)?.getAttribute("aria-checked") === "true";
}

function bindToggle(id, cb) {
  const el = $(id);
  if (!el) return;
  el.addEventListener("click", () => {
    if (el.disabled) return;
    const on = !isOn(id);
    setToggle(id, on);
    markDirty();
    cb?.(on);
  });
}

function markDirty() {
  state.dirty = true;
  const b = $("saveState");
  b.textContent = "未保存";
  b.className = "badge warn";
  updatePathState();
}

function updatePathState() {
  const changed = pathsChanged();
  const b = $("pathState");
  if (changed) {
    b.textContent = "已修改 · 需校验";
    b.className = "badge warn";
  } else {
    b.textContent = "自动读取";
    b.className = "badge warn";
  }
}

function pathsChanged() {
  const fields = ["ggufPath", "packPath", "plePath", "tokenizerPath", "exePath", "profilePath"];
  return fields.some((id) => ($(id)?.value || "") !== (state.original[id] || ""));
}

function value(id) { return ($(id)?.value ?? "").trim(); }

function activeModelPort() {
  return Number(state.live?.process?.active_port || state.live?.config?.port || state.payload?.view?.port || numberValue("port", 8080));
}
function numberValue(id, fallback = 0) {
  const n = Number(value(id));
  return Number.isFinite(n) ? n : fallback;
}

function buildForm() {
  return {
    model_name: value("modelName"),
    exe: value("exePath"),
    native: value("ggufPath"),
    ple: value("plePath"),
    pack: value("packPath"),
    tokenizer: value("tokenizerPath"),
    expert_profile: value("profilePath"),
    log: value("logPath"),
    context: numberValue("contextInput", 0),
    kv: value("kvSelect"),
    vision: $("visionCheck").checked,
    vision_gpu: value("visionDevice") === "gpu",
    vision_exe: value("visionExe"),
    vision_mmproj: value("visionMmproj"),
    vision_max_tokens: numberValue("visionMaxTokens", 768),
    vision_threads: numberValue("visionThreads", 8),
    idle_unload_min: numberValue("idleUnload", 60),
    reasoning_effort: value("reasoningEffort"),
    temperature: value("temperature"),
    top_p: value("topP"),
    top_k: value("topK"),
    seed: value("seed"),
    max_tokens: value("maxTokens"),
    fit_max_tokens: isOn("fitMaxTokens"),
    low_ram: value("lowRam"),
    gpu: value("gpu"),
    mtp: isOn("mtpEnabled"),
    mtp_path: value("mtpPath"),
    spec: value("spec"),
    spec_min_p: value("specMinP"),
    prefill: value("prefill"),
    expert_cache: value("expertCache"),
    kv_resident: value("kvResident"),
    vram_reserve: numberValue("vramReserve", 0),
    host: value("host"),
    port: numberValue("port", 8080),
    api_key: value("apiKey"),
    api_key_clear: state.clearKey,
    cors_origins: value("corsOrigins"),
    trusted_origins: value("trustedOrigins"),
    allowed_hosts: value("allowedHosts"),
    auto_restart: isOn("autoRestart"),
    lazy_on_restart: isOn("lazyRestart"),
    start_mode: value("startMode"),
    open_browser: isOn("openBrowser"),
    prevent_sleep: isOn("preventSleep"),
    api_monitor: isOn("apiMonitor"),
    backup_count: numberValue("backupCount", 5),
  };
}

function setField(id, v) {
  const el = $(id);
  if (!el) return;
  if (el.type === "checkbox") el.checked = !!v;
  else el.value = v ?? "";
}

function fillConfig(payload) {
  state.payload = payload;
  const v = payload.view || {};
  const configs = payload.configs || [];
  const sel = $("configSelect");
  sel.innerHTML = "";
  for (const c of configs) {
    const o = document.createElement("option");
    o.value = c.path;
    o.textContent = c.name;
    o.title = c.name + (c.model_name ? ` · ${c.model_name}` : "");
    if (c.path === payload.active) o.selected = true;
    sel.appendChild(o);
  }
  if (!configs.length) {
    const o = document.createElement("option");
    o.textContent = "未找到 strata-*.json";
    o.value = "";
    sel.appendChild(o);
  }

  setField("modelName", v.model_name);
  setField("ggufPath", v.native);
  setField("packPath", v.pack);
  setField("plePath", v.ple);
  setField("tokenizerPath", v.tokenizer);
  setField("exePath", v.exe);
  setField("profilePath", v.expert_profile);
  setField("logPath", v.log);
  setField("contextInput", v.context || "");
  setField("kvSelect", v.kv || "int8");
  setField("idleUnload", v.idle_unload_min ?? 60);
  setField("lowRam", v.low_ram || "off");
  setField("gpu", v.gpu || "");
  setToggle("mtpEnabled", v.mtp);
  setField("mtpPath", v.mtp_kind === "count" ? (v.mtp_value || "") : (v.mtp_path || v.mtp_detected_path || ""));
  $("mtpPath").disabled = !v.mtp;
  updateMtpHint();
  setField("spec", v.spec || "");
  setField("specMinP", v.spec_min_p || "");
  setField("prefill", v.prefill || "");
  setField("expertCache", v.expert_cache || "");
  setField("kvResident", v.kv_resident || "");
  setField("vramReserve", v.vram_reserve || 0);
  setField("visionCheck", v.vision);
  setField("visionDevice", v.vision_gpu ? "gpu" : "cpu");
  setField("visionExe", v.vision_exe || "");
  setField("visionMmproj", v.vision_mmproj || "");
  setField("visionMaxTokens", v.vision_max_tokens || 768);
  setField("visionThreads", v.vision_threads || 8);
  setField("reasoningEffort", v.reasoning_effort || "");
  setField("temperature", v.temperature ?? "");
  setField("topP", v.top_p ?? "");
  setField("topK", v.top_k ?? "");
  setField("seed", v.seed ?? "");
  setField("maxTokens", v.max_tokens || "");
  setToggle("fitMaxTokens", v.fit_max_tokens);
  setField("startMode", v.start_mode || "preload");
  setField("backupCount", v.backup_count || 5);
  setField("host", v.host || "127.0.0.1");
  setField("port", v.port || 8080);
  const lan = !["127.0.0.1", "localhost", "::1"].includes(v.host || "127.0.0.1");
  setField("corsOrigins", v.cors_origins || "");
  setField("trustedOrigins", v.trusted_origins || "");
  setField("allowedHosts", v.allowed_hosts || "");
  setField("apiKey", "");
  state.apiKeySet = !!v.api_key_set;
  const prefix = v.api_key_prefix || "sk-strata-…";
  $("apiKey").placeholder = v.api_key_set ? `已设置 ${prefix}（留空保持不变）` : "未设置";
  $("keyState").textContent = v.api_key_set ? `已设置 · ${prefix}` : "未设置";
  state.clearKey = false;
  updateCorsHint();

  setToggle("autoRestart", v.auto_restart);
  setToggle("lazyRestart", v.lazy_on_restart);
  $("lazyRestart").disabled = !v.auto_restart;
  setToggle("openBrowser", v.open_browser);
  setToggle("preventSleep", v.prevent_sleep);
  setToggle("apiMonitor", v.api_monitor);
  setToggle("apiMonitorToggle", v.api_monitor);

  setToggle("lanToggle", lan);
  $("lanExtra").classList.toggle("open", lan);
  if (!Array.from($("host").options).some((o) => o.value === v.host)) {
    const o = document.createElement("option");
    o.value = v.host;
    o.textContent = v.host;
    $("host").appendChild(o);
  }
  setField("host", v.host || "127.0.0.1");
  updateLanBadge(lan);

  updateVisionNote();
  $("visionDevice").disabled = !$("visionCheck").checked;
  $("visionExe").disabled = !$("visionCheck").checked;
  $("visionMmproj").disabled = !$("visionCheck").checked;
  $("visionMaxTokens").disabled = !$("visionCheck").checked;
  $("visionThreads").disabled = !$("visionCheck").checked;
  updateVisionHint();
  updateOptionalVisibility();

  $("backupBadge").textContent = `${v.backup_count || 5} 个备份`;
  setText("manageSummary", `${v.config_name || "未选择配置"}${v.model_name ? ` · ${v.model_name}` : ""}${v.context ? ` · ${Math.round(v.context / 1024)}K` : ""}${v.kv ? ` · KV ${v.kv}` : ""}`);
  $("contextHint").textContent = v.context ? `当前 ${Math.round(v.context / 1024)}K` : "当前配置";
  $("contextNote").textContent = v.context
    ? `当前 ${Math.round(v.context / 1024)}K；可点“自动配置”查看官方建议`
    : "从配置读取，可手动修改";

  state.original = {};
  for (const id of ["ggufPath", "packPath", "plePath", "tokenizerPath", "exePath", "profilePath"]) {
    state.original[id] = $(id)?.value || "";
  }
  state.dirty = false;
  const b = $("saveState");
  b.textContent = "已保存";
  b.className = "badge ok";
  $("pathState").textContent = "自动读取";
  $("pathState").className = "badge warn";
  updateButtons();
  renderWarnings(v.warnings || []);
}

function apiBaseUrl() {
  const port = numberValue("port", 8080);
  const on = isOn("lanToggle");
  const urls = state.payload?.view?.lan_urls || [];
  if (on) {
    const host = urls.length ? urls[0] : "http://本机IP";
    return `${host}:${port}/v1`;
  }
  return `http://127.0.0.1:${port}/v1`;
}

function updateVisionNote() {
  const el = $("visionNote");
  if (!el) return;
  const checked = $("visionCheck").checked;
  const ready = !!(value("visionExe") && value("visionMmproj"));
  if (!checked) {
    el.textContent = "图片输入未启用；勾选后保存并重启生效。";
    el.className = "hint warn";
  } else if (!ready) {
    el.textContent = "缺少视觉编码器；请在高级性能中指定 exe 和 mmproj。";
    el.className = "hint warn";
  } else if (value("visionDevice") === "gpu") {
    el.textContent = "将使用 GPU 编码；需要 CUDA 版 strata-vision，保存并重启后生效。";
    el.className = "hint warn";
  } else {
    el.textContent = "视觉组件已启用：CPU 编码（不占显存，单图较慢）";
    el.className = "hint ok";
  }
}

function updateOptionalVisibility() {
  const mtp = $("mtpOptions");
  if (mtp) mtp.classList.toggle("open", isOn("mtpEnabled"));
  const vision = $("visionOptions");
  if (vision) vision.classList.toggle("open", !!$("visionCheck")?.checked);
}

function updateMtpHint() {
  const el = $("mtpHint");
  if (!el) return;
  const on = isOn("mtpEnabled");
  const path = value("mtpPath");
  const v = state.payload?.view || {};
  if (!on) {
    if (v.mtp_available && v.mtp_detected_path) {
      el.textContent = `检测到 MTP runtime：${v.mtp_detected_path}；打开后保存并重启生效`;
      el.className = "hint ok";
    } else {
      el.textContent = "MTP 未启用";
      el.className = "hint";
    }
    return;
  }
  if (/^\d+$/.test(path)) {
    el.textContent = `MTP 草稿数 = ${path}（Qwen3.6 等 own-path 模型）`;
    el.className = "hint ok";
    return;
  }
  if (!path) {
    el.textContent = "请填写 MTP runtime 目录（需包含 experts.bin）";
    el.className = "hint warn";
    return;
  }
  const known = path === v.mtp_path || path === v.mtp_detected_path;
  if (known && !v.mtp_available) {
    el.textContent = "目录已配置，但缺少 experts.bin；请等 MTP 安装完成后再启用";
    el.className = "hint warn";
  } else {
    el.textContent = `保存后使用：${path}`;
    el.className = "hint ok";
  }
}

function updateVisionHint(showToast = false) {
  const el = $("visionDeviceHint");
  if (!el) return;
  const gpu = value("visionDevice") === "gpu";
  const ready = !!(value("visionExe") && value("visionMmproj"));
  const free = state.live?.status?.metrics?.engine?.vram_free_mib;
  if (!ready) {
    el.textContent = "需要视觉 exe 与 mmproj；GPU 编码还需要 CUDA 版 strata-vision。";
    el.className = "hint warn";
    return;
  }
  if (gpu) {
    const freeText = free != null ? `当前显存剩余约 ${fmt(free)} MiB。` : "";
    el.textContent = `GPU 编码需要重新编译 CUDA 版 strata-vision（当前是 CPU 构建）。${freeText}GPU 编码约需 1.4 GB 显存，建议降低上下文/专家缓存或继续使用 CPU。`;
    el.className = "hint warn";
    if (showToast) toast("GPU 编码需要 CUDA 版 strata-vision；当前编码器是 CPU 构建", "warn", 7000);
  } else {
    el.textContent = "当前使用 CPU 编码，不占显存；单张图片约数秒。改用 GPU 需重新编译 CUDA 版。";
    el.className = "hint ok";
  }
}

function updateCorsHint() {
  const el = $("corsHint");
  if (!el) return;
  const cors = value("corsOrigins");
  const hasKey = state.apiKeySet || !!value("apiKey");
  if (!cors) {
    el.textContent = "CORS 当前关闭。填写来源后保存并重启生效。";
    el.className = "hint";
  } else if (cors.split(",").map((x) => x.trim()).includes("*") && !hasKey) {
    el.textContent = "CORS 为 * 且未设置 API Key：任意网页都可以调用本地模型 API，风险较高。";
    el.className = "hint warn";
  } else {
    el.textContent = "CORS 已配置；保存并重启生效。";
    el.className = "hint ok";
  }
}

function updateApiBase() {
  const base = apiBaseUrl();
  const root = base.replace(/\/v1$/, "");
  const fields = {
    apiBase: base,
    chatEndpoint: `${root}/v1/chat/completions`,
    anthropicEndpoint: `${root}/v1/messages`,
  };
  for (const [id, text] of Object.entries(fields)) {
    const el = $(id);
    if (el) el.value = text;
  }
}

function updateLanBadge(on) {
  $("lanBadge").textContent = on ? "局域网" : "仅本机";
  $("lanBadge").className = "badge " + (on ? "info" : "off");
  $("lanExtra").classList.toggle("open", on);
  const port = numberValue("port", 8080);
  const urls = state.payload?.view?.lan_urls || [];
  $("lanUrl").textContent = on
    ? (urls.length ? `${urls[0]}:${port}` : `http://本机IP:${port}`)
    : "—";
  updateApiBase();
}

async function copyText(text) {
  try {
    await navigator.clipboard.writeText(text);
    return true;
  } catch (e) {
    try {
      const ta = document.createElement("textarea");
      ta.value = text;
      ta.style.position = "fixed";
      ta.style.opacity = "0";
      document.body.appendChild(ta);
      ta.select();
      const ok = document.execCommand("copy");
      ta.remove();
      return ok;
    } catch (e2) {
      return false;
    }
  }
}

async function copyField(id, label) {
  const text = value(id);
  if (await copyText(text)) toast(`${label} 已复制`, "ok");
  else toast("复制失败，请手动选择地址", "warn");
}

async function copyApi() {
  await copyField("apiBase", "API Base");
}

async function copyKey() {
  let key = value("apiKey");
  if (!key) {
    if (!state.apiKeySet) {
      toast("尚未设置 API Key", "warn");
      return;
    }
    try {
      const r = await post("/api/api-key", {});
      key = r.api_key || "";
    } catch (e) {
      toast(`读取 API Key 失败: ${e.message}`, "err", 7000);
      return;
    }
  }
  if (key && await copyText(key)) toast("API Key 已复制", "ok");
  else toast("复制失败，请手动选择", "warn");
}

function renderWarnings(list) {
  // Inline warnings are shown in the relevant hints; this is a compact toast only when needed.
  if (!list.length) return;
  // Avoid a toast on every poll: only when the set changes.
  const key = list.join("|");
  if (state.lastError === key) return;
  state.lastError = key;
  toast(list[0], "warn", 7000);
}

function stateInfo(s) {
  const p = s.process || {};
  const st = s.status || {};
  const h = st.health && typeof st.health === "object" ? st.health : {};
  const m = st.metrics && typeof st.metrics === "object" ? st.metrics : {};
  return { p, st, h, m, live: m.live || {}, hw: m.hardware || {}, hs: m.hardware_static || {}, eng: m.engine || {} };
}

function render(s) {
  state.live = s;
  const { p, st, h, m, live, hw, hs, eng } = stateInfo(s);
  const cv = s.config || {};
  const raw = p.state || "stopped";
  const stLabel = stateLabels[raw] || raw;
  $("statusPill").dataset.state = raw;
  setText("statusText", stLabel);
  setText("statusSub", `${cv.host || "127.0.0.1"}:${cv.port || 8080}`);
  setText("consolePort", `:${s.app?.ui_port || 8090}`);

  const model = h.model || eng.model || cv.model_name || "—";
  const ctx = Number(eng.max_context || cv.context || 0);
  const images = !!h.image_generation || !!h.images || !!cv.vision;
  const idleMin = Number(cv.idle_unload_min || 0);
  setText("workspaceSub", `${model} · ${ctx ? Math.round(ctx / 1024) + "K" : "上下文未知"} · 图片${images ? "开启" : "关闭"} · API Key ${cv.api_key_set ? "已启用" : "未设置"}${idleMin ? ` · 空闲 ${fmt(idleMin)} 分钟卸载` : ""}`);

  const uptime = Number(p.uptime_s || 0);
  const upText = uptime ? `${String(Math.floor(uptime / 3600)).padStart(2, "0")}:${String(Math.floor(uptime % 3600 / 60)).padStart(2, "0")}:${String(uptime % 60).padStart(2, "0")}` : "—";
  const engineVer = eng.engine || eng.version || h.version || "";
  setText("serviceState", stLabel);
  setText("serviceSub", p.owned ? `PID ${p.pid || "—"} · ${upText} · ${engineVer ? `引擎 ${engineVer}` : "引擎 —"}` : (raw === "external" ? "由外部启动，只读监控" : (p.detail || "等待启动")));
  setText("modelState", h.loaded ? "已加载" : (raw === "stopped" || raw === "error" ? "未启动" : (raw === "unloaded" ? "未加载" : "加载中")));
  setText("modelSub", `${model} · ${ctx ? Math.round(ctx / 1024) + "K" : "—"} · ${images ? "图片开启" : "图片关闭"}`);

  const last = (m.requests && m.requests[0]) || null;
  const used = live.state === "idle"
    ? Number(last?.prompt_tokens || 0) + Number(last?.output_tokens || 0)
    : Number(live.prompt_tokens || 0) + Number(live.generated || 0);
  const frac = pct(used, ctx);
  setText("ctxValue", `${used ? (used >= 1000 ? fmt(used / 1000, 1) + "K" : fmt(used)) : "0"}${ctx ? ` / ${Math.round(ctx / 1024)}K` : ""}`);
  $("ctxBar").style.width = `${frac}%`;
  setText("ctxSub", last ? `最近请求 ${fmt(last.prompt_tokens)} prompt · ${fmt(last.output_tokens)} output` : "暂无请求");
  $("ctxGauge").setAttribute("stroke-dasharray", `${(235.6 * frac / 100).toFixed(1)} 314.2`);
  setText("ctxPct", `${Math.round(frac)}%`);
  setText("gaugeSub", `${fmt(used)} / ${ctx || "—"} tokens`);

  renderLive(live, last);
  renderMetrics(hw, hs, eng, m, cv);
  renderRequests(m.requests || [], m.totals || null);
  renderLogPreview(st.last_log || "");
  updateVisionHint();
  updateMtpHint();
  updateButtons();
}

function renderLive(live, last) {
  const s = live.state || "idle";
  const badge = $("liveState");
  badge.textContent = liveLabels[s] || s || "空闲";
  badge.className = "badge " + (s === "generating" ? "ok" : s === "reading" ? "info" : s === "queued" ? "warn" : "plain");
  let tokens = "—", speed = "—", bar = 0, meta = "等待请求", elapsed = "";
  if (s === "reading") {
    const total = Number(live.prompt_total || 0);
    const read = Number(live.prompt_read || 0);
    tokens = total ? `${fmt(read)} / ${fmt(total)} tokens` : `${fmt(live.prompt_tokens)} tokens`;
    bar = total ? pct(read, total) : 0;
    speed = live.prefill_tok_s_mean ? `${fmt(live.prefill_tok_s_mean)} tok/s` : "—";
    elapsed = live.elapsed_s ? `${fmt(live.elapsed_s, 1)}s` : "";
    meta = `预填中 · ${fmt(bar)}%`;
  } else if (s === "generating") {
    tokens = `${fmt(live.generated)} / ${fmt(live.max_tokens)} tokens`;
    bar = live.max_tokens ? pct(live.generated, live.max_tokens) : 0;
    speed = `${fmt(live.tok_s, 1)} tok/s`;
    elapsed = live.elapsed_s ? `${fmt(live.elapsed_s, 1)}s` : "";
    meta = `阶段 ${live.phase || "decode"} · 预填 ${fmt(live.prefill_tok_s_mean, 1)} tok/s · 队列 ${fmt(live.queued)}`;
  } else if (last) {
    tokens = `${fmt(last.output_tokens)} tokens`;
    speed = last.decode_tok_s ? `${fmt(last.decode_tok_s, 1)} tok/s` : "—";
    meta = `最近请求 · prompt ${fmt(last.prompt_tokens)} · 输出 ${fmt(last.output_tokens)}`;
  }
  setText("liveTokens", tokens);
  setText("liveSpeed", speed);
  setText("liveElapsed", elapsed);
  $("liveBar").style.width = `${bar}%`;
  setText("liveMeta", meta);
}

function tone(el, p) {
  if (!el) return;
  el.classList.remove("warn", "danger");
  if (p >= 92) el.classList.add("danger");
  else if (p >= 80) el.classList.add("warn");
}

function metric(id, val, unit, sub, barId, barPct) {
  $(id).innerHTML = `${val == null || val === "—" ? "—" : esc(val)}${unit && val !== "—" ? `<small>${esc(unit)}</small>` : ""}`;
  setText(id + "Sub", sub || "");
  const p = Math.max(0, Math.min(100, barPct || 0));
  const b = barId || (id.replace(/^m/, "m") + "Bar");
  if (b && $(b)) { $(b).style.width = `${p}%`; tone($(b), p); }
}

function renderMetrics(hw, hs, eng, m, cv) {
  const gpu = hw.gpu_util;
  metric("mGpu", gpu == null ? "—" : fmt(gpu), "%", hs.gpu_name || "GPU", "mGpuBar", gpu || 0);
  const vramUsed = hw.gpu_mem_used, vramTotal = hw.gpu_mem_total;
  metric("mVram", vramUsed == null ? "—" : gb(vramUsed), vramTotal ? `/ ${gb(vramTotal, 0)} GB` : "GB",
         eng.expert_slots ? `${fmt(eng.expert_slots)} 专家缓存` : "显存", "mVramBar", pct(vramUsed, vramTotal));
  const temp = hw.gpu_temp;
  metric("mTemp", temp == null ? "—" : fmt(temp), "°C", hw.gpu_power != null ? `功耗 ${fmt(hw.gpu_power)} W` : "GPU 温度", "mTempBar", pct(temp, 90));
  const power = hw.gpu_power;
  metric("mPower", power == null ? "—" : fmt(power), "W", hw.gpu_power_limit ? `限制 ${fmt(hw.gpu_power_limit)} W` : "功耗", "mPowerBar", pct(power, hw.gpu_power_limit || 250));
  metric("mCpu", hw.cpu == null ? "—" : fmt(hw.cpu), "%", hs.threads ? `${hs.cores || "?"} 核 · ${hs.threads} 线程` : "CPU", "mCpuBar", hw.cpu || 0);
  metric("mRam", hw.ram_used == null ? "—" : gb(hw.ram_used), hw.ram_total ? `/ ${gb(hw.ram_total, 0)} GB` : "GB", "系统 RAM", "mRamBar", pct(hw.ram_used, hw.ram_total));
  const disk = hw.disk_read_mb;
  metric("mDisk", disk == null ? "—" : fmt(disk, disk < 10 ? 1 : 0), "MB/s", hw.disk_write_mb != null ? `写入 ${fmt(hw.disk_write_mb, 1)} MB/s` : "磁盘读取", "mDiskBar", Math.min(100, (disk || 0) / 1024 * 100));
  const gen = hw.gpu_pcie_gen_max || hw.gpu_pcie_gen;
  metric("mPcie", gen ? `Gen${gen}` : "—", hw.gpu_pcie_width ? `x${hw.gpu_pcie_width}` : "", hw.gpu_pcie_rx_mb != null ? `接收 ${fmt(hw.gpu_pcie_rx_mb, hw.gpu_pcie_rx_mb < 10 ? 1 : 0)} MB/s` : "PCIe", "mPcieBar", Math.min(100, (hw.gpu_pcie_rx_mb || 0) / 2048 * 100));

  const cache = Number(eng.expert_cache_mib || 0);
  setText("expertsText", eng.expert_slots ? `${fmt(eng.expert_slots)} · ${fmt(cache / 1024, 1)} GB` : "—");
  const expertsPct = pct(cache, Number(hw.gpu_mem_total || 0) / 1048576);
  $("expertsBar").style.width = `${expertsPct}%`; tone($("expertsBar"), expertsPct);
  setText("ramText", hw.ram_total ? `${gb(hw.ram_used)} / ${gb(hw.ram_total, 0)} GB` : "—");
  const ramPct = pct(hw.ram_used, hw.ram_total);
  $("ramBar").style.width = `${ramPct}%`; tone($("ramBar"), ramPct);
  setText("tempText", temp == null ? "—" : `${fmt(temp)} °C`);
  const tempPct = pct(temp, 90);
  $("tempBar").style.width = `${tempPct}%`; tone($("tempBar"), tempPct);
}

function finishBadge(finish) {
  const map = {
    stop: ["ok", "Done"], length: ["ok", "Max tokens"], cancel: ["warn", "Stopped"],
    disconnect: ["warn", "Closed"], error: ["danger", "Error"],
  };
  const [cls, label] = map[finish] || ["plain", finish || "—"];
  return `<span class="badge ${cls}">${esc(label)}</span>`;
}

function requestRows(reqs, details) {
  if (!reqs.length) return `<tr><td colspan="${details ? 6 : 9}" class="muted">暂无请求</td></tr>`;
  return reqs.map((r) => {
    const t = r.time ? new Date(r.time * 1000).toLocaleTimeString("zh-CN", { hour: "2-digit", minute: "2-digit", second: "2-digit" }) : "—";
    if (details) {
      return `<tr><td>${esc(t)}</td><td class="mono">${esc(r.endpoint || "API")}</td><td>${finishBadge(r.finish)}</td><td class="num">${fmt(r.prompt_tokens)}</td><td class="num">${fmt(r.output_tokens)}</td><td class="num">${fmt(r.duration_s, 1)}s</td></tr>`;
    }
    const fresh = r.prompt_tokens == null ? null : Math.max(0, Number(r.prompt_tokens) - Number(r.reused || 0));
    const prefill = fresh > 0 && r.prompt_ms > 0 ? fresh / (r.prompt_ms / 1000) : null;
    const hit = r.hit_rate == null ? "—" : `${(Number(r.hit_rate) * 100).toFixed(1)}%`;
    return `<tr><td>${esc(t)}</td><td>${finishBadge(r.finish)}</td><td class="num">${fmt(r.prompt_tokens)}</td><td class="num">${fmt(r.reused)}</td><td class="num">${fmt(r.output_tokens)}</td><td class="num">${fmt(prefill)}</td><td class="num">${fmt(r.decode_tok_s, 1)}</td><td class="num">${esc(hit)}</td><td class="num">${fmt(r.duration_s, 1)}s</td></tr>`;
  }).join("");
}

function renderRequests(reqs, totals) {
  const list = reqs.slice(0, 12);
  $("reqBody").innerHTML = requestRows(list, false);
  $("reqTabBody").innerHTML = requestRows(list, true);
  setText("reqCount", reqs.length);
  setText("reqSummary", reqs.length ? `最近 ${Math.min(12, reqs.length)} 条` : "暂无请求");
  setText("reqTabSummary", reqs.length ? `最近 ${Math.min(12, reqs.length)} 条元数据` : "暂无请求");
  if (totals && totals.requests) {
    const since = totals.since ? new Date(totals.since * 1000).toLocaleString("zh-CN") : "";
    setText("reqTotals", `累计 ${fmt(totals.requests)} 请求 · ${fmt(totals.output_tokens)} 输出 tokens${since ? ` · 自 ${since}` : ""}`);
  } else {
    setText("reqTotals", "");
  }
  setText("stateUpdated", state.live?.now ? `更新于 ${state.live.now}` : "");
}

function renderLogPreview(lastLine) {
  const lines = state.logLines.length ? state.logLines.slice(-6) : (lastLine ? [lastLine] : []);
  if (!lines.length) {
    $("logPreview").textContent = "等待启动…";
    return;
  }
  $("logPreview").innerHTML = lines.map((line) => {
    const cls = /WARNING|WARN|error|failed|ERR/i.test(line) ? (/error|failed|ERR/i.test(line) ? "err" : "warn") : (/ready|session is up|loaded/i.test(line) ? "ok" : "");
    return cls ? `<span class="${cls}">${esc(line)}</span>` : esc(line);
  }).join("\n");
}

function renderLogs() {
  const out = $("consoleOutput");
  if (!state.logLines.length) {
    out.textContent = "等待启动…";
    return;
  }
  out.innerHTML = state.logLines.map((line) => {
    const cls = /WARNING|WARN/i.test(line) ? "warn" : (/error|failed|ERR/i.test(line) ? "err" : (/ready|session is up|loaded/i.test(line) ? "ok" : "dim"));
    return `<span class="${cls}">${esc(line)}</span>`;
  }).join("\n");
  if (!state.logPaused) out.scrollTop = out.scrollHeight;
  setText("logPathLabel", state.logPath || "strata / server + engine");
  setText("logUpdated", `更新于 ${new Date().toLocaleTimeString("zh-CN")}`);
}

function updateButtons() {
  const p = state.live?.process || {};
  const raw = p.state || "stopped";
  const owned = !!p.owned;
  const hasConfig = !!(state.payload?.active || state.live?.config?.config_path);
  const canStart = hasConfig && !["starting", "loading", "running", "unloaded", "stopping", "restarting", "external"].includes(raw);
  $("startBtn").disabled = !canStart;
  $("startBtn").textContent = (raw === "stopped" || raw === "error") ? "启动" : (raw === "external" ? "外部服务" : "运行中");
  $("stopBtn").disabled = !(owned && !["stopped", "stopping"].includes(raw));
  $("loadBtn").disabled = !["running", "unloaded", "external"].includes(raw);
  $("unloadBtn").disabled = raw !== "running";
  $("restartBtn").disabled = !(raw === "running" || raw === "loading" || raw === "unloaded");
  $("openWebBtn").disabled = raw === "stopped" || raw === "error";
  $("apiMonitorLabel").textContent = isOn("apiMonitor") ? "api_monitor 开启" : "api_monitor 关闭";
  $("apiMonitorState").textContent = isOn("apiMonitor") ? "已开启" : "默认关闭";
  $("apiMonitorState").className = "badge " + (isOn("apiMonitor") ? "ok" : "plain");
}

async function pollState() {
  try {
    const s = await api("/api/state");
    render(s);
    if (s.app?.init_error && !s.config?.config_path) {
      setText("workspaceSub", s.app.init_error);
    }
    // If the active config changed on disk/by another tab, refresh the form.
    if (s.config?.config_path && state.payload?.active && s.config.config_path !== state.payload.active) {
      await loadConfig(true);
    }
  } catch (e) {
    $("statusPill").dataset.state = "error";
    setText("statusText", "控制台连接失败");
    setText("statusSub", e.message);
  }
}

async function pollLogs() {
  if (state.logPaused) return;
  try {
    const j = await api("/api/logs?lines=300");
    state.logLines = j.lines || [];
    state.logPath = j.path || "";
    renderLogs();
    if (state.tab !== "console") renderLogPreview("");
  } catch (e) { /* the state poll reports connection errors */ }
}

async function loadConfig(quiet = false) {
  try {
    const payload = await api("/api/config");
    fillConfig(payload);
    if (!quiet) toast("配置已刷新", "ok");
  } catch (e) {
    toast(`读取配置失败: ${e.message}`, "err", 7000);
  }
}

async function saveConfig(quiet = false) {
  const data = buildForm();
  if (!data.native && !data.pack) throw new Error("请先选择模型或 Pack 目录");
  const payload = await post("/api/config", data);
  fillConfig(payload);
  if (!quiet) toast("配置已保存，备份已生成", "ok");
  return payload;
}

async function startServer() {
  try {
    if (pathsChanged()) {
      if (!confirm("检测到模型/Pack/Tokenizer/引擎路径被修改。\n\n路径不匹配可能导致启动失败或输出异常。仍要继续吗？")) return;
    }
    await saveConfig(true);
    await post("/api/start", { lazy: value("startMode") === "lazy" });
    toast("服务启动中", "ok");
    await pollState();
  } catch (e) {
    toast(`启动失败: ${e.message}`, "err", 8000);
  }
}

async function restartServer() {
  try {
    if (pathsChanged()) {
      if (!confirm("路径已修改，重启后服务可能无法加载。仍要继续吗？")) return;
    }
    await saveConfig(true);
    await post("/api/restart", { lazy: value("startMode") === "lazy" });
    toast("服务重启中", "ok");
    await pollState();
  } catch (e) {
    toast(`重启失败: ${e.message}`, "err", 8000);
  }
}

async function stopServer() {
  if (!confirm("停止模型服务？这会释放显存。")) return;
  try {
    await post("/api/stop");
    toast("服务已停止", "ok");
    await pollState();
  } catch (e) {
    toast(`停止失败: ${e.message}`, "err", 7000);
  }
}

async function loadModel() {
  try { await post("/api/load"); toast("正在加载模型", "ok"); await pollState(); }
  catch (e) { toast(`加载失败: ${e.message}`, "err", 7000); }
}

async function unloadModel() {
  try { await post("/api/unload"); toast("模型已卸载，下一次请求会重新加载", "ok"); await pollState(); }
  catch (e) { toast(`卸载失败: ${e.message}`, "err", 7000); }
}

async function openTarget(target) {
  try { await post("/api/open", { target }); }
  catch (e) { toast(`打开失败: ${e.message}`, "err", 6000); }
}

async function browse(kind, targetId, title) {
  try {
    const r = await post("/api/browse", { kind, title, current: value(targetId) });
    if (r.path) {
      setField(targetId, r.path);
      markDirty();
    }
  } catch (e) { toast(`选择失败: ${e.message}`, "err", 6000); }
}

function showModal(title, body, buttons = []) {
  setText("modalTitle", title);
  $("modalBody").innerHTML = body;
  const foot = $("modalFoot");
  foot.innerHTML = "";
  for (const b of buttons) {
    const el = document.createElement("button");
    el.className = `st-btn ${b.primary ? "st-btn--primary" : "st-btn--secondary"}`;
    el.textContent = b.label;
    el.addEventListener("click", () => b.onClick?.());
    foot.appendChild(el);
  }
  $("modalBackdrop").hidden = false;
}

function closeModal() { $("modalBackdrop").hidden = true; }

async function autoConfig() {
  try {
    toast("正在读取硬件与 Strata 推荐…", "warn", 3000);
    const r = await post("/api/autoconfig");
    if (!r.ok) throw new Error(r.error || "检测失败");
    const hw = r.hardware || {};
    const rec = r.recommendation || {};
    const gpu = (hw.gpus && hw.gpus[0]) || {};
    const currentCtx = numberValue("contextInput", 0);
    const suggested = Number(rec.context || 0);
    const rows = [];
    rows.push(["GPU", gpu.name ? `${gpu.name} · ${gpu.vram_gb} GB` : "—"]);
    rows.push(["RAM", hw.ram_gb ? `${hw.ram_gb} GB` : "—"]);
    rows.push(["CPU", hw.cpu?.name || "—"]);
    rows.push(["官方安装推荐", rec.title ? `${rec.title} · ${suggested ? Math.round(suggested / 1024) + "K" : "—"}` : "—"]);
    rows.push(["官方上下文建议", suggested ? `${Math.round(suggested / 1024)}K` : "—"]);
    rows.push(["当前上下文", currentCtx ? `${Math.round(currentCtx / 1024)}K` : "—"]);
    const table = `<table class="diff"><tbody>${rows.map(([k, v]) => `<tr><th>${esc(k)}</th><td>${esc(v)}</td></tr>`).join("")}</tbody></table>`;
    const note = `<p class="muted small">自动配置只调整运行参数，不会更换模型或修改路径。${rec.why ? `<br>${esc(rec.why)}` : ""}</p>`;
    showModal("自动配置预览", table + note, [
      { label: "取消", onClick: closeModal },
      { label: "应用推荐", primary: true, onClick: () => {
          if (suggested > 0) setField("contextInput", suggested);
          if (!["fp16", "int8", "q4_0", "k8v4"].includes(value("kvSelect"))) setField("kvSelect", "int8");
          if (!value("visionExe") || !value("visionMmproj")) $("visionCheck").checked = false;
          markDirty();
          closeModal();
          toast("已应用推荐值，保存后生效", "ok");
        } },
    ]);
  } catch (e) {
    toast(`自动配置失败: ${e.message}`, "err", 8000);
  }
}

async function showBackups() {
  try {
    const j = await api("/api/backups");
    const items = j.backups || [];
    if (!items.length) {
      showModal("备份管理", `<p class="muted">还没有备份。保存配置后会生成带时间戳的备份。</p>`, [
        { label: "打开备份目录", onClick: () => { closeModal(); openTarget("backups"); } },
        { label: "关闭", primary: true, onClick: closeModal },
      ]);
      return;
    }
    const list = `<div class="modal-list">${items.map((b) => `<div class="item"><div class="meta"><b>${esc(b.name)}</b><span>${esc(b.mtime_text)} · ${esc(b.size)} · ${esc(b.model_name || "—")} · ${b.context ? Math.round(b.context / 1024) + "K" : "—"} · ${esc(b.kv || "—")}</span></div><button class="st-btn st-btn--secondary st-btn--sm" data-restore="${esc(b.path)}">恢复</button></div>`).join("")}</div>`;
    showModal("备份管理", list, [
      { label: "打开备份目录", onClick: () => { closeModal(); openTarget("backups"); } },
      { label: "关闭", primary: true, onClick: closeModal },
    ]);
    $("modalBody").querySelectorAll("[data-restore]").forEach((btn) => {
      btn.addEventListener("click", async () => {
        const path = btn.getAttribute("data-restore");
        if (!confirm("恢复这个备份？当前配置会先自动备份。")) return;
        try {
          const payload = await post("/api/backups/restore", { path });
          fillConfig(payload);
          closeModal();
          toast("备份已恢复", "ok");
        } catch (e) { toast(`恢复失败: ${e.message}`, "err", 7000); }
      });
    });
  } catch (e) { toast(`读取备份失败: ${e.message}`, "err", 7000); }
}

function quitConsole() {
  showModal("退出控制台后台服务", `<p>这会终止运行在 8090 的 Strata Console 后台服务，不是只关闭当前管理页面。</p><p class="muted small">仅退出后台服务时，模型 API 继续运行，但自动重启监督会停止；下次启动控制台会自动识别为外部服务。</p>`, [
    { label: "取消", onClick: closeModal },
    { label: "仅退出后台服务", onClick: async () => { try { await post("/api/quit", { stop_model: false }); closeModal(); setText("statusText", "后台服务已退出"); } catch (e) { toast(`退出失败: ${e.message}`, "err", 7000); } } },
    { label: "退出并停止模型", primary: true, onClick: async () => { try { await post("/api/quit", { stop_model: true }); closeModal(); setText("statusText", "已退出并停止"); } catch (e) { toast(`退出失败: ${e.message}`, "err", 7000); } } },
  ]);
}

async function showCommand() {
  try {
    const c = await api("/api/command");
    const engine = Array.isArray(c.engine_args) ? c.engine_args.join(" ") : "";
    showModal("启动命令预览", `<div class="muted small" style="margin-bottom:8px">服务命令</div><pre>${esc(c.server || "")}</pre><div class="muted small" style="margin:12px 0 8px">引擎参数</div><pre>${esc(engine || "（由配置提供）")}</pre>`, [
      { label: "关闭", primary: true, onClick: closeModal },
    ]);
  } catch (e) { toast(`读取命令失败: ${e.message}`, "err", 7000); }
}

function bindEvents() {
  document.querySelectorAll(".settings-tab").forEach((tab) => {
    tab.addEventListener("click", () => {
      document.querySelectorAll(".settings-tab").forEach((x) => {
        x.classList.toggle("active", x === tab);
        x.setAttribute("aria-selected", x === tab ? "true" : "false");
      });
      document.querySelectorAll(".settings-page").forEach((page) => {
        page.classList.toggle("active", page.id === "page-" + tab.dataset.panel);
      });
      $("settingsScroll").scrollTop = 0;
    });
  });

  document.querySelectorAll(".adv-toggle").forEach((btn) => {
    btn.addEventListener("click", () => {
      const body = $(btn.dataset.target);
      const open = body.classList.toggle("open");
      btn.classList.toggle("open", open);
    });
  });

  document.querySelectorAll(".st-tab").forEach((tab) => {
    tab.addEventListener("click", () => {
      document.querySelectorAll(".st-tab").forEach((x) => x.setAttribute("aria-selected", x === tab ? "true" : "false"));
      document.querySelectorAll(".panel").forEach((p) => p.classList.toggle("active", p.id === "panel-" + tab.dataset.tab));
      state.tab = tab.dataset.tab;
      if (state.tab === "console") renderLogs();
      if (state.tab === "requests") pollState();
    });
  });

  const settings = $("settingsScroll");
  settings.addEventListener("input", (e) => {
    if (e.target.matches("input, select")) { markDirty(); }
  });
  settings.addEventListener("change", (e) => {
    if (e.target.matches("input, select")) { markDirty(); }
  });

  $("configSelect").addEventListener("change", async () => {
    try {
      const payload = await post("/api/select", { path: $("configSelect").value });
      fillConfig(payload);
      toast("已切换配置档", "ok");
      await pollState();
    } catch (e) { toast(`切换失败: ${e.message}`, "err", 7000); }
  });

  $("contextPreset").addEventListener("change", () => {
    if ($("contextPreset").value) {
      setField("contextInput", $("contextPreset").value);
      markDirty();
    }
    $("contextPreset").value = "";
  });

  $("lanToggle").addEventListener("click", () => {
    const on = !isOn("lanToggle");
    setToggle("lanToggle", on);
    $("host").value = on ? "0.0.0.0" : "127.0.0.1";
    updateLanBadge(on);
    markDirty();
  });

  bindToggle("autoRestart", (on) => {
    $("lazyRestart").disabled = !on;
    if (!on) setToggle("lazyRestart", true);
  });
  bindToggle("lazyRestart");
  bindToggle("fitMaxTokens");
  bindToggle("mtpEnabled", (on) => {
    $("mtpPath").disabled = !on;
    if (on && !value("mtpPath")) setField("mtpPath", state.payload?.view?.mtp_detected_path || "");
    updateMtpHint();
    updateOptionalVisibility();
  });
  $("mtpPath").addEventListener("input", updateMtpHint);
  bindToggle("openBrowser");
  bindToggle("preventSleep");
  bindToggle("apiMonitor", (on) => {
    setToggle("apiMonitorToggle", on);
    updateButtons();
  });
  bindToggle("apiMonitorToggle", (on) => {
    setToggle("apiMonitor", on);
    updateButtons();
  });

  $("visionCheck").addEventListener("change", () => {
    $("visionDevice").disabled = !$("visionCheck").checked;
    $("visionExe").disabled = !$("visionCheck").checked;
    $("visionMmproj").disabled = !$("visionCheck").checked;
    $("visionMaxTokens").disabled = !$("visionCheck").checked;
    $("visionThreads").disabled = !$("visionCheck").checked;
    updateVisionNote();
    updateVisionHint();
    updateOptionalVisibility();
    markDirty();
  });
  $("visionDevice").addEventListener("change", () => { updateVisionNote(); updateVisionHint(true); });
  $("visionExe").addEventListener("input", () => { updateVisionNote(); updateVisionHint(); });
  $("visionMmproj").addEventListener("input", () => { updateVisionNote(); updateVisionHint(); });

  $("apiKey").addEventListener("input", () => {
    state.clearKey = false;
    if (value("apiKey")) {
      state.apiKeySet = true;
      $("keyState").textContent = "保存后生效";
    }
    updateCorsHint();
  });

  $("genKey").addEventListener("click", () => {
    const a = new Uint8Array(24);
    crypto.getRandomValues(a);
    setField("apiKey", "sk-strata-" + Array.from(a, (x) => x.toString(16).padStart(2, "0")).join(""));
    state.clearKey = false;
    state.apiKeySet = true;
    $("keyState").textContent = "保存后生效（可先点“复制”）";
    updateCorsHint();
    markDirty();
  });
  $("copyKeyBtn").addEventListener("click", copyKey);
  $("clearKey").addEventListener("click", () => {
    state.clearKey = true;
    state.apiKeySet = false;
    setField("apiKey", "");
    $("keyState").textContent = "保存后清除";
    updateCorsHint();
    markDirty();
  });
  $("corsOrigins").addEventListener("input", updateCorsHint);

  $("saveBtn").addEventListener("click", async () => {
    try { await saveConfig(); await pollState(); }
    catch (e) { toast(`保存失败: ${e.message}`, "err", 8000); }
  });
  $("startBtn").addEventListener("click", startServer);
  $("stopBtn").addEventListener("click", stopServer);
  $("restartBtn").addEventListener("click", restartServer);
  $("loadBtn").addEventListener("click", loadModel);
  $("unloadBtn").addEventListener("click", unloadModel);
  $("openWebBtn").addEventListener("click", () => {
    window.open(`http://127.0.0.1:${activeModelPort()}/`, "_blank", "noopener");
  });
  $("autoConfigBtn").addEventListener("click", autoConfig);
  $("backupsBtn").addEventListener("click", showBackups);
  $("openBackupDirBtn").addEventListener("click", () => openTarget("backups"));
  $("commandBtn").addEventListener("click", showCommand);
  $("quitConsoleBtn").addEventListener("click", quitConsole);
  $("openOfficialMonitor").addEventListener("click", () => {
    if (!isOn("apiMonitor")) {
      toast("请先开启 api_monitor 并重启服务", "warn");
      return;
    }
    window.open(`http://127.0.0.1:${activeModelPort()}/api-monitor`, "_blank", "noopener");
  });
  $("browseNative").addEventListener("click", () => browse("file", "ggufPath", "选择 GGUF 首分片"));
  $("browsePack").addEventListener("click", () => browse("folder", "packPath", "选择 Pack 目录"));
  $("browseMtp").addEventListener("click", async () => {
    await browse("folder", "mtpPath", "选择 MTP runtime 目录");
    updateMtpHint();
  });
  $("copyApiBtn").addEventListener("click", copyApi);
  $("copyChatBtn").addEventListener("click", () => copyField("chatEndpoint", "Chat 端点"));
  $("copyAnthropicBtn").addEventListener("click", () => copyField("anthropicEndpoint", "Anthropic 端点"));
  $("copyModelBtn").addEventListener("click", () => copyField("modelName", "模型名"));
  $("modelName").addEventListener("input", updateApiBase);
  $("port").addEventListener("input", updateApiBase);
  $("host").addEventListener("change", () => {
    const on = value("host") !== "127.0.0.1";
    setToggle("lanToggle", on);
    updateLanBadge(on);
    markDirty();
  });
  $("openConfigBtn").addEventListener("click", () => openTarget("config"));
  $("openConfigBtn2").addEventListener("click", () => openTarget("config"));
  $("openLogBtn").addEventListener("click", () => openTarget("log"));
  $("reloadBtn").addEventListener("click", () => loadConfig());
  $("configRefresh").addEventListener("click", () => loadConfig());
  $("modalClose").addEventListener("click", closeModal);
  $("modalBackdrop").addEventListener("click", (e) => { if (e.target === $("modalBackdrop")) closeModal(); });

  $("pauseLog").addEventListener("click", () => {
    state.logPaused = !state.logPaused;
    $("pauseLog").textContent = state.logPaused ? "继续滚动" : "暂停滚动";
    $("logFollow").textContent = state.logPaused ? "已暂停" : "实时跟随";
    $("logFollow").className = "badge " + (state.logPaused ? "warn" : "ok");
  });
  $("clearLog").addEventListener("click", () => {
    state.logLines = [];
    $("consoleOutput").textContent = "视图已清空；新日志会继续追加。";
  });

  const themeBtn = $("themeBtn");
  try {
    const saved = localStorage.getItem("strata.console.theme");
    if (saved) document.documentElement.dataset.theme = saved;
  } catch (e) {}
  syncTheme();
  themeBtn.addEventListener("click", () => {
    document.documentElement.dataset.theme = document.documentElement.dataset.theme === "dark" ? "light" : "dark";
    try { localStorage.setItem("strata.console.theme", document.documentElement.dataset.theme); } catch (e) {}
    syncTheme();
  });
}

function syncTheme() {
  const dark = document.documentElement.dataset.theme === "dark";
  $("themeIcon").setAttribute("href", dark ? "#i-sun" : "#i-moon");
}

async function boot() {
  bindEvents();
  try {
    const s = await api("/api/session");
    state.csrf = s.token;
    await loadConfig(true);
    await pollState();
    await pollLogs();
    state.pollTimer = setInterval(pollState, 1500);
    setInterval(pollLogs, 1500);
  } catch (e) {
    toast(`控制台初始化失败: ${e.message}`, "err", 10000);
    setText("statusText", "初始化失败");
  }
}

document.addEventListener("DOMContentLoaded", boot);

