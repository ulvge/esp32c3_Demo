#ifndef STYLE_CSS_H
#define STYLE_CSS_H

const char g_styleCSS[] PROGMEM = R"rawliteral(
* {
  box-sizing: border-box;
}

body {
  font-family: -apple-system, "Segoe UI", "Microsoft YaHei", sans-serif;
  background: #f2f4f7;
  color: #333;
  margin: 0;
  padding: 16px;
  line-height: 1.6;
  -webkit-text-size-adjust: 100%;
}

h2 {
  margin: 0 0 16px;
  color: #2c3e50;
  border-bottom: 2px solid #3498db;
  padding-bottom: 8px;
  font-size: 20px;
}

h3 {
  color: #2c3e50;
  margin: 20px 0 8px;
  font-size: 15px;
}

.card {
  background: #fff;
  border-radius: 10px;
  padding: 14px 16px;
  margin-bottom: 12px;
  box-shadow: 0 2px 6px rgba(0, 0, 0, 0.07);
}

/* ---------- RSSI 大数字 ---------- */
.rssi-main {
  display: flex;
  align-items: baseline;
  justify-content: center;
  gap: 6px;
}

.rssi-num {
  font-size: 52px;
  font-weight: 700;
  color: #2c3e50;
  line-height: 1.1;
  font-variant-numeric: tabular-nums;
}

.rssi-unit {
  font-size: 16px;
  color: #95a5a6;
}

.bar {
  height: 10px;
  background: #e8edf2;
  border-radius: 6px;
  overflow: hidden;
  margin: 10px 0 12px;
}

.bar-fill {
  height: 100%;
  width: 0%;
  border-radius: 6px;
  transition: width 0.3s ease, background-color 0.3s ease;
  background: #b0bec5;
}

.bar-fill.lv1 { background: #e74c3c; }
.bar-fill.lv2 { background: #f39c12; }
.bar-fill.lv3 { background: #27ae60; }

.row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  flex-wrap: wrap;
}

.tag {
  display: inline-block;
  padding: 3px 12px;
  border-radius: 20px;
  font-size: 13px;
  font-weight: 600;
}

.tag-near { background: #e8f8f0; color: #1e8449; }
.tag-far { background: #fdf3e3; color: #b9770e; }
.tag-none { background: #f0f2f5; color: #8795a1; }

.muted {
  font-size: 12px;
  color: #95a5a6;
}

/* ---------- 键值行 ---------- */
.kv {
  display: flex;
  justify-content: space-between;
  gap: 10px;
  padding: 5px 0;
  border-bottom: 1px dashed #eef1f4;
  font-size: 14px;
}

.kv:last-child { border-bottom: none; }

.kv > span { color: #7b8a97; flex: 0 0 40%; }

.kv > b {
  color: #2c3e50;
  font-weight: 600;
  text-align: right;
  word-break: break-all;
  flex: 1 1 auto;
}

/* ---------- 输入区 ---------- */
label {
  display: inline-block;
  min-width: 84px;
  font-size: 14px;
  color: #555;
}

input[type="text"] {
  padding: 8px 10px;
  font-size: 15px;
  border: 1px solid #ccd4db;
  border-radius: 6px;
  width: calc(100% - 96px);
  max-width: 260px;
  outline: none;
}

input[type="text"]:focus {
  border-color: #3498db;
  box-shadow: 0 0 4px rgba(52, 152, 219, 0.4);
}

button {
  padding: 8px 18px;
  font-size: 15px;
  background: #3498db;
  color: #fff;
  border: none;
  border-radius: 6px;
  cursor: pointer;
  margin-top: 8px;
}

button:hover { background: #2980b9; }
button:active { background: #1f6dad; }

.result-box {
  background: #f0f9f0;
  border-left: 4px solid #27ae60;
  padding: 10px 12px;
  border-radius: 6px;
  font-family: "Consolas", "Courier New", monospace;
  font-size: 14px;
  min-height: 38px;
  word-break: break-all;
}

.tip {
  font-size: 12px;
  color: #95a5a6;
  margin: 16px 0 8px;
  line-height: 1.7;
}

/* ---------- 附近设备列表 ---------- */
.list-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  margin-bottom: 6px;
}

.list-title {
  font-size: 14px;
  font-weight: 600;
  color: #2c3e50;
}

button.mini {
  padding: 4px 12px;
  font-size: 12px;
  margin: 0;
  background: #eef2f6;
  color: #5b6b7a;
}

button.mini:hover { background: #e1e8ee; color: #2c3e50; }

.peers { margin-top: 8px; }

.peer {
  padding: 8px 10px;
  border: 1px solid #eef1f4;
  border-radius: 8px;
  margin-bottom: 6px;
  cursor: pointer;
  transition: background 0.15s, border-color 0.15s;
}

.peer:hover { background: #f7fafd; }
.peer.on { border-color: #3498db; background: #eef6fd; }

.peer-main {
  display: flex;
  justify-content: space-between;
  gap: 8px;
  font-size: 14px;
}

.peer-main b {
  color: #2c3e50;
  font-weight: 600;
  word-break: break-all;
}

.peer-sub {
  font-size: 11px;
  color: #95a5a6;
  margin-top: 2px;
  word-break: break-all;
}

.rssi-sm {
  color: #2c3e50;
  font-weight: 600;
  white-space: nowrap;
  font-variant-numeric: tabular-nums;
}

.dim { color: #b0bec5; font-style: normal; }

/* ---------- 白名单 ---------- */
.id-form { margin-top: 10px; }

.id-form input[type="text"] {
  width: 100%;
  max-width: none;
  margin-bottom: 6px;
}

.id-badge {
  display: inline-block;
  background: #e8f8f0;
  color: #1e8449;
  border-radius: 6px;
  padding: 1px 6px;
  margin-right: 6px;
  font-size: 12px;
  font-weight: 600;
}

.mini-del {
  color: #e74c3c;
  font-size: 12px;
  cursor: pointer;
  white-space: nowrap;
}

/* ---------- 抓取 IRK ---------- */
.cap-msg {
  margin-top: 8px;
  padding: 8px 10px;
  border-radius: 8px;
  background: #f1f5f9;
  color: #2c3e50;
  font-size: 13px;
  line-height: 1.5;
  word-break: break-all;
}

#capIrk {
  margin-top: 8px;
  font-family: ui-monospace, Menlo, Consolas, monospace;
  font-size: 12px;
  color: #1e8449;
  word-break: break-all;
}

@media (min-width: 720px) {
  body { max-width: 680px; margin: 0 auto; padding: 24px 16px; }
}
)rawliteral";

#endif
