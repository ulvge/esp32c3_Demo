#ifndef __INDEX_HTML_H
#define __INDEX_HTML_H

// 网页 HTML 内容，独立存放，修改这里即可
// PROGMEM 表示把这段字符串放在 Flash 里而不是 RAM 里，节省 RAM 空间
const char g_indexHTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 控制台</title>
  <link rel="stylesheet" href="/style.css">
</head>
<body>
  <h2>ESP32 控制台</h2>

  <div class="card">
    <label>输入内容1：</label>
    <input type="text" id="userInput1" placeholder="在这里输入...">
    <button onclick="sendData1()">发送</button>
  </div>

  <div class="card">
    <label>输入内容2：</label>
    <input type="text" id="userInput2" placeholder="在这里输入...">
    <button onclick="sendData2()">发送</button>
  </div>

  <h3>操作结果：</h3>
  <div id="result" class="result-box">（暂无操作）</div>

  <h3>实时状态：</h3>
  <div id="status" class="status-box">加载中...</div>

  <script>
    function sendData1() {
      var input = document.getElementById('userInput1').value;
      fetch('/submit?box=1&text=' + encodeURIComponent(input))
        .then(r => r.text())
        .then(data => {
          document.getElementById('result').innerHTML = data;
        })
        .catch(() => {
          document.getElementById('result').innerHTML = '发送失败';
        });
    }

    function sendData2() {
      var input = document.getElementById('userInput2').value;
      fetch('/submit?box=2&text=' + encodeURIComponent(input))
        .then(r => r.text())
        .then(data => {
          document.getElementById('result').innerHTML = data;
        })
        .catch(() => {
          document.getElementById('result').innerHTML = '发送失败';
        });
    }

    // ---- 每 0.5 秒拉取一次实时状态 ----
    function refreshStatus() {
      fetch('/status')
        .then(r => r.json())
        .then(data => {
          document.getElementById('status').innerHTML =
            '芯片温度：' + data.temp.toFixed(1) + ' °C<br>' +
            '运行时间：' + data.uptime + ' 秒<br>' +
            '最后收到：' + (data.lastText || '（还没收到输入）');
        })
        .catch(() => {
          // 网络抖动时不要清空显示，避免闪烁
        });
    }

    setInterval(refreshStatus, 500);   // 每 0.5 秒刷新一次
    refreshStatus();                   // 页面打开立即刷一次
  </script>
</body>
</html>
)rawliteral";

#endif