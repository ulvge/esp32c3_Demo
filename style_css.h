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
  padding: 20px;
  line-height: 1.6;
}

h2 {
  margin-top: 0;
  color: #2c3e50;
  border-bottom: 2px solid #3498db;
  padding-bottom: 8px;
}

h3 {
  color: #2c3e50;
  margin-top: 24px;
}

.card {
  background: #fff;
  border-radius: 8px;
  padding: 16px 20px;
  margin-bottom: 16px;
  box-shadow: 0 2px 6px rgba(0, 0, 0, 0.08);
}

label {
  display: inline-block;
  min-width: 90px;
  font-size: 14px;
  color: #555;
}

input[type="text"] {
  padding: 8px 10px;
  font-size: 15px;
  border: 1px solid #ccc;
  border-radius: 4px;
  width: 60%;
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
  border-radius: 4px;
  cursor: pointer;
  margin-left: 8px;
}

button:hover {
  background: #2980b9;
}

button:active {
  background: #1f6dad;
}

hr {
  border: none;
  border-top: 1px solid #e0e0e0;
  margin: 24px 0;
}

.status-box {
  background: #fafbfc;
  border-left: 4px solid #3498db;
  padding: 10px 14px;
  border-radius: 4px;
  font-family: "Consolas", "Courier New", monospace;
  font-size: 14px;
  min-height: 60px;
}

.result-box {
  background: #f0f9f0;
  border-left: 4px solid #27ae60;
  padding: 10px 14px;
  border-radius: 4px;
  font-family: "Consolas", "Courier New", monospace;
  font-size: 14px;
  min-height: 40px;
}
)rawliteral";

#endif