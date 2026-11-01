#ifndef __INDEX_HTML_H
#define __INDEX_HTML_H

// 网页 HTML 内容，独立存放，修改这里即可
const char g_indexHTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32-C3 蓝牙信号检测</title>
  <link rel="stylesheet" href="/style.css">
</head>
<body>
  <h2>ESP32-C3 蓝牙信号检测</h2>

  <div class="card">
    <div class="rssi-main">
      <span id="rssiVal" class="rssi-num">--</span>
      <span class="rssi-unit">dBm</span>
    </div>
    <div class="bar"><div id="rssiBar" class="bar-fill" style="width:0%"></div></div>
    <div class="row">
      <span id="nearTag" class="tag tag-far">未检测</span>
      <span id="linkTag" class="muted">正在连接设备…</span>
    </div>
  </div>

  <div class="card">
    <div class="kv"><span>跟踪设备</span><b id="devName">--</b></div>
    <div class="kv"><span>MAC</span><b id="devMac">--</b></div>
    <div class="kv"><span>最后收到</span><b id="devAge">--</b></div>
    <div class="kv"><span>累计广播包</span><b id="pktCount">--</b></div>
    <div class="kv"><span>原始 RSSI（未平滑）</span><b id="rawRssi">--</b></div>
    <div class="kv"><span>匹配方式</span><b id="matchMode">--</b></div>
    <div class="kv"><span>身份识别</span><b id="idTag">--</b></div>
    <div class="kv"><span>地址类型</span><b id="addrType">--</b></div>
  </div>

  <div class="card">
    <div class="list-head">
      <span class="list-title">白名单（认出我的手机）</span>
      <span class="muted" id="idCount">0/8</span>
    </div>
    <div id="idList" class="peers"><div class="muted">还没有配置</div></div>
    <div class="id-form">
      <input type="text" id="idName" placeholder="名称，例如 haha">
      <input type="text" id="idIrk" placeholder="IRK：32 位十六进制（换地址也能认）">
      <input type="text" id="idMac" placeholder="MAC：AA:BB:CC:DD:EE:FF（固定地址设备用）">
      <button onclick="addId()">加入白名单</button>
    </div>
    <div class="muted" id="idMsg"></div>
  </div>

  <div class="card">
    <div class="list-head">
      <span class="list-title">抓取 IRK（让手机配对一次，自动读走）</span>
      <span class="muted" id="capStTag">未开始</span>
    </div>
    <div class="cap-msg" id="capMsg">点“开始抓取”，再用手机蓝牙搜索并连接 <b>C3-IRK-CAP</b>，弹窗点配对即可。</div>
    <div class="id-form">
      <input type="text" id="capName" placeholder="名称，例如 haha" value="haha">
      <button onclick="capStart()">开始抓取</button>
      <button class="mini" onclick="capStop()">停止</button>
    </div>
    <div class="muted" id="capIrk"></div>
  </div>

  <div class="card">
    <div class="list-head">
      <span class="list-title">附近设备（点一行锁定跟踪）</span>
      <button class="mini" onclick="clearLock()">解除锁定</button>
    </div>
    <div class="muted" id="lockTip">扫描中…</div>
    <div id="peerList" class="peers"><div class="muted">扫描中…</div></div>
  </div>

  <div class="card">
    <div class="kv"><span>检测模式</span><b id="srcTag">--</b></div>
    <div class="kv"><span>芯片温度</span><b id="tempVal">--</b></div>
    <div class="kv"><span>运行时间</span><b id="uptimeVal">--</b></div>
    <div class="kv"><span>剩余内存</span><b id="heapVal">--</b></div>
    <div class="kv"><span>历史最低内存</span><b id="heapMinVal">--</b></div>
  </div>

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

  <p class="tip">
    提示：手机熄屏后系统会降低蓝牙广播频率，RSSI 可能长时间不变或显示 -100；
    把手机蓝牙打开并停留在蓝牙设置页，刷新会更稳定。
  </p>

  <script>
    var POLL_MS = 500;
    var polling = false;
    var lastOkAt = 0;

    function setText(id, v) {
      var el = document.getElementById(id);
      if (el) el.textContent = v;
    }

    function fmtAge(ms) {
      if (ms === null || ms === undefined || ms < 0) return '从未收到';
      if (ms < 800) return '刚刚';
      return (ms / 1000).toFixed(1) + ' 秒前';
    }

    // 地址类型说明：直接决定“锁 MAC 有没有用”
    var ADDR_TXT = [
      'public 固定（可长期锁定）',
      'static 随机（一般固定，设备重启可能变）',
      'RPA 可解析随机（约 15 分钟换一次）',
      'NRPA 不可解析随机（会变，无法锁定）'
    ];

    function esc(s) {
      return String(s == null ? '' : s)
        .replace(/&/g, '&amp;').replace(/</g, '&lt;')
        .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
    }

    function render(d) {
      var rssi = Number(d.currentRSSI);

      setText('rssiVal', isNaN(rssi) ? '--' : rssi);
      setText('rawRssi', (d.rawRSSI === undefined || d.rawRSSI === null) ? '--' : d.rawRSSI + ' dBm');
      setText('srcTag', d.rssiSource || '--');
      setText('devName', d.devName ? d.devName : '（未广播名字，只能按 MAC 认）');
      setText('devMac', d.devMac ? d.devMac : '--');
      setText('devAge', fmtAge(d.lastSeenMs));
      setText('pktCount', (d.packets || 0) + ' 个');
      setText('addrType', ADDR_TXT[d.addrType] || '--');

      var modeTxt = '自动跟踪最强信号';
      if (d.lockMode === 'manual') modeTxt = '已手动锁定（网页点选）';
      else if (d.lockMode === 'cfg-mac') modeTxt = '已锁定配置里的 MAC';
      else if (d.lockMode === 'cfg-name') {
        modeTxt = d.nameMatched
          ? '已按名字命中：' + (d.keyword || '')
          : '按名字「' + (d.keyword || '') + '」未命中 → 临时跟最强';
      }
      if (d.lockMode === 'manual' && !d.devMac) modeTxt = '已手动锁定，但目标不在附近';
      if (d.whitelisted) modeTxt = '白名单命中（按身份识别，与地址无关）';
      setText('matchMode', modeTxt);
      setText('idTag', d.whitelisted ? ('✔ ' + (d.devName || '白名单设备')) : '未识别');
      setText('uptimeVal', d.uptime || '--');
      setText('heapVal', (d.heap !== undefined ? Math.round(d.heap / 1024) + ' KB' : '--'));
      setText('heapMinVal', (d.heapMin !== undefined ? Math.round(d.heapMin / 1024) + ' KB' : '--'));

      if (d.temp === null || d.temp === undefined || isNaN(Number(d.temp))) {
        setText('tempVal', '不支持 / 读取失败');
      } else {
        setText('tempVal', Number(d.temp).toFixed(1) + ' °C');
      }

      // 信号条：-100dBm -> 0%，-40dBm -> 100%
      var pct = 0;
      if (!isNaN(rssi)) {
        pct = ((rssi + 100) / 60) * 100;
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
      }
      var bar = document.getElementById('rssiBar');
      bar.style.width = pct.toFixed(0) + '%';
      bar.className = 'bar-fill ' + (pct > 66 ? 'lv3' : (pct > 33 ? 'lv2' : 'lv1'));

      var tag = document.getElementById('nearTag');
      if (d.isDevNear) {
        tag.textContent = '在附近';
        tag.className = 'tag tag-near';
      } else if (isNaN(rssi) || rssi <= -100) {
        tag.textContent = '未检测到设备';
        tag.className = 'tag tag-none';
      } else {
        tag.textContent = '较远';
        tag.className = 'tag tag-far';
      }

      lastOkAt = Date.now();
      setText('linkTag', '已连接 · 更新于 ' + new Date().toLocaleTimeString('zh-CN', { hour12: false }));
    }

    // ---- 每 0.5 秒拉取一次实时状态（带重叠保护，网络慢时不会堆积请求）----
    function refreshStatus() {
      if (polling) return;
      polling = true;

      fetch('/status?_=' + Date.now(), { cache: 'no-store' })
        .then(function (r) {
          if (!r.ok) throw new Error('HTTP ' + r.status);
          return r.json();
        })
        .then(render)
        .catch(function () {
          // 断线时不清空数据，只提示；避免页面一直闪
          if (Date.now() - lastOkAt > 3000) {
            setText('linkTag', '连接中断，正在重试…');
          }
        })
        .then(function () { polling = false; });
    }

    function post(boxId, text) {
      return fetch('/submit?box=' + boxId + '&text=' + encodeURIComponent(text))
        .then(function (r) { return r.text(); });
    }

    function sendData1() {
      post(1, document.getElementById('userInput1').value)
        .then(function (t) { document.getElementById('result').textContent = t; })
        .catch(function () { document.getElementById('result').textContent = '发送失败'; });
    }

    function sendData2() {
      post(2, document.getElementById('userInput2').value)
        .then(function (t) { document.getElementById('result').textContent = t; })
        .catch(function () { document.getElementById('result').textContent = '发送失败'; });
    }

    // ---- 附近设备列表：点一行就锁定 ----
    var peersPolling = false;
    var ADDR_SHORT = ['固定', '静态随机', 'RPA', '随机'];

    function pick(mac) {
      fetch('/select?mac=' + encodeURIComponent(mac), { cache: 'no-store' })
        .then(function (r) { return r.json(); })
        .then(function () { refreshPeers(); refreshStatus(); })
        .catch(function () {});
    }

    function clearLock() {
      fetch('/select?mac=', { cache: 'no-store' })
        .then(function (r) { return r.json(); })
        .then(function () { refreshPeers(); refreshStatus(); })
        .catch(function () {});
    }

    function renderPeers(d) {
      var box = document.getElementById('peerList');
      var tip = document.getElementById('lockTip');
      var lock = d.lock || 'auto';
      var lockedMac = d.lockedMac || '';
      var km = d.keyword ? '「' + d.keyword + '」' : '';

      var hint = '当前：自动跟踪最强信号（没有指定目标）';
      if (lock === 'manual') hint = '当前：已锁定 ' + lockedMac;
      else if (lock === 'cfg-mac') hint = '当前：配置里的 MAC ' + lockedMac;
      else if (lock === 'cfg-name') {
        hint = d.peers && d.peers.length
          ? '当前：按名字 ' + km + ' 匹配（下面点一行可改成锁 MAC）'
          : '当前：按名字 ' + km + ' 匹配';
      }
      if (d.reacq) hint += ' · 目标换了随机地址，已自动接管（对准那行点一下即可固定）';
      tip.textContent = hint;

      var ps = d.peers || [];
      if (!ps.length) {
        box.innerHTML = '<div class="muted">附近没有扫描到设备</div>';
        return;
      }

      var html = '';
      for (var i = 0; i < ps.length; i++) {
        var p = ps[i];
        var on = (p.m === lockedMac) || p.tg;
        html += '<div class="peer' + (on ? ' on' : '') + '" onclick="pick(\'' + p.m + '\')">'
          + '<div class="peer-main"><b>'
          + (p.id ? '<span class="id-badge">✔ ' + esc(p.id) + '</span>' : '')
          + (p.n ? esc(p.n) : '<i class="dim">（未广播名字）</i>') + '</b>'
          + '<span class="rssi-sm">' + p.r + ' dBm</span></div>'
          + '<div class="peer-sub">' + p.m + ' · ' + (ADDR_SHORT[p.t] || '?')
          + ' · ' + p.p + ' 包 · ' + p.a + 'ms 前</div>'
          + '</div>';
      }
      box.innerHTML = html;
    }

    function refreshPeers() {
      if (peersPolling) return;
      peersPolling = true;
      fetch('/peers?_=' + Date.now(), { cache: 'no-store' })
        .then(function (r) { if (!r.ok) throw new Error('HTTP ' + r.status); return r.json(); })
        .then(renderPeers)
        .catch(function () {})
        .then(function () { peersPolling = false; });
    }

    // ---- 白名单管理 ----
    var idsPolling = false;

    function renderIds(d) {
      var box = document.getElementById('idList');
      document.getElementById('idCount').textContent = (d.items ? d.items.length : 0) + '/' + (d.max || 8);
      if (!d.items || !d.items.length) {
        box.innerHTML = '<div class="muted">还没有配置。手机请填 IRK（地址每 15 分钟会变，只有 IRK 能认出来）。</div>';
        return;
      }
      var html = '';
      for (var i = 0; i < d.items.length; i++) {
        var it = d.items[i];
        html += '<div class="peer"><div class="peer-main"><b>' + esc(it.n) + '</b>'
          + '<span class="mini-del" onclick="event.stopPropagation();delId(' + it.i + ')">删除</span></div>'
          + '<div class="peer-sub">'
          + (it.irk ? 'IRK ' + it.irk : '')
          + (it.irk && it.m ? ' · ' : '')
          + (it.m ? 'MAC ' + it.m : '')
          + '</div></div>';
      }
      box.innerHTML = html;
    }

    function refreshIds() {
      if (idsPolling) return;
      idsPolling = true;
      fetch('/ids?_=' + Date.now(), { cache: 'no-store' })
        .then(function (r) { if (!r.ok) throw new Error('HTTP ' + r.status); return r.json(); })
        .then(renderIds)
        .catch(function () {})
        .then(function () { idsPolling = false; });
    }

    function addId() {
      var msg = document.getElementById('idMsg');
      var n = document.getElementById('idName').value;
      var irk = document.getElementById('idIrk').value;
      var mac = document.getElementById('idMac').value;
      msg.textContent = '提交中…';
      fetch('/id_add?name=' + encodeURIComponent(n) + '&irk=' + encodeURIComponent(irk)
            + '&mac=' + encodeURIComponent(mac))
        .then(function (r) {
          if (!r.ok) return r.text().then(function (t) { throw new Error(t); });
          return r.json();
        })
        .then(function () {
          msg.textContent = '已加入白名单，下一步扫描到它就会显示 ✔';
          document.getElementById('idIrk').value = '';
          document.getElementById('idMac').value = '';
          refreshIds();
          refreshPeers();
        })
        .catch(function (e) { msg.textContent = e.message || '添加失败'; });
    }

    function delId(i) {
      fetch('/id_del?i=' + i, { cache: 'no-store' })
        .then(function () { document.getElementById('idMsg').textContent = '已删除'; refreshIds(); refreshPeers(); })
        .catch(function () {});
    }

    // ---- 抓取 IRK（ESP32 当外设，让手机把 IRK 发过来）----
    var CAP_TEXT = ['未开始', '广播中，等待手机连接', '手机已连接，正在配对…', '已配对，正在读取 IRK…', '抓取成功', '失败', '正在停止扫描、准备外设…'];
    var capPolling = false;
    var capLastState = -1;

    function capStart() {
      var n = document.getElementById('capName').value;
      document.getElementById('capStTag').textContent = '启动中…';
      document.getElementById('capIrk').textContent = '';
      fetch('/cap_start?name=' + encodeURIComponent(n), { cache: 'no-store' })
        .then(function (r) {
          if (!r.ok) throw new Error('启动失败');
          return r.json();
        })
        .then(function () {
          capLastState = -1;
          refreshCap();
        })
        .catch(function (e) { document.getElementById('capMsg').textContent = e.message || '启动失败'; });
    }

    function capStop() {
      fetch('/cap_stop', { cache: 'no-store' })
        .then(function () { refreshCap(); })
        .catch(function () {});
    }

    function refreshCap() {
      if (capPolling) return;
      capPolling = true;
      fetch('/cap_state?_=' + Date.now(), { cache: 'no-store' })
        .then(function (r) { if (!r.ok) throw new Error('HTTP ' + r.status); return r.json(); })
        .then(function (d) {
          document.getElementById('capStTag').textContent = CAP_TEXT[d.st] || '?';
          document.getElementById('capMsg').textContent = d.msg || '';
          if (d.st === 4 && d.irk) {
            document.getElementById('capIrk').textContent = 'IRK：' + d.irk + (d.mac ? '（手机地址 ' + d.mac + '）' : '');
            // 抓完刷新白名单，让用户马上看到新条目
            if (capLastState !== 4) { refreshIds(); refreshPeers(); }
          }
          capLastState = d.st;
        })
        .catch(function () {})
        .then(function () { capPolling = false; });
    }

    setInterval(refreshStatus, POLL_MS);
    setInterval(refreshPeers, 2000);
    setInterval(refreshIds, 5000);
    setInterval(refreshCap, 1200);
    refreshStatus();
    refreshPeers();
    refreshIds();
    refreshCap();
  </script>
</body>
</html>
)rawliteral";

#endif
