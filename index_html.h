#ifndef INDEX_HTML_H
#define INDEX_HTML_H

// 网页 HTML 内容，独立存放，修改这里即可
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 控制台</title>
</head>
<body>
  <h2>ESP32 控制台</h2>

  <label>输入内容：</label>
  <input type="text" id="userInput" placeholder="在这里输入...">
  <button onclick="sendData()">发送</button>

  <hr>
  <h3>状态显示：</h3>
  <div id="status">等待操作...</div>

  <script>
    function sendData() {
      var input = document.getElementById('userInput').value;
      fetch('/submit?text=' + encodeURIComponent(input))
        .then(r => r.text())
        .then(data => {
          document.getElementById('status').innerHTML = data;
        })
        .catch(() => {
          document.getElementById('status').innerHTML = '发送失败';
        });
    }
  </script>
</body>
</html>
)rawliteral";

#endif