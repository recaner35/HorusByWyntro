var gateway = `ws://${window.location.hostname}/ws`;
var ws;
var currentDirection = 0; // 0: CW, 1: CCW, 2: Bi-Directional
var isRunning = false;

window.addEventListener('load', onLoad);

function onLoad(event) {
    initWebSocket();
    // Load saved settings
    const savedLang = localStorage.getItem('selectedLanguage') || 'en';
    document.getElementById('languageSelect').value = savedLang;
    changeLanguage(savedLang);

    // Initial Tab
    showTab('dashboard');
}

// =======================
// WEBSOCKET
// =======================
function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    ws = new WebSocket(gateway);
    ws.onopen = onOpen;
    ws.onclose = onClose;
    ws.onmessage = onMessage;
}

function onOpen(event) {
    console.log('Connection opened');
    document.getElementById('connectionStatus').className = 'connection-status connected';
}

function onClose(event) {
    console.log('Connection closed');
    document.getElementById('connectionStatus').className = 'connection-status disconnected';
    setTimeout(initWebSocket, 2000); // Retry connection
}

function onMessage(event) {
    console.log("Received: " + event.data);
    var data = JSON.parse(event.data);

    // DASHBOARD UPDATES
    if (data.hasOwnProperty('running')) {
        isRunning = data.running;
        updateStatusUI();
    }
    if (data.hasOwnProperty('tpd')) {
        document.getElementById('tpdPayload').value = data.tpd;
        updateTpdDisplay(data.tpd);
    }
    if (data.hasOwnProperty('dur')) {
        document.getElementById('durPayload').value = data.dur;
        updateDurDisplay(data.dur);
    }
    if (data.hasOwnProperty('dir')) {
        currentDirection = data.dir;
        updateDirectionUI();
    }

    // PEER LIST UPDATES (ESP-NOW)
    if (data.hasOwnProperty('peers')) {
        renderDeviceList(data.peers);
    }
}

// =======================
// TABS & NAVIGATION
// =======================
function showTab(tabId) {
    // Hide all tabs
    document.querySelectorAll('.tab-content').forEach(el => el.classList.remove('active'));
    // Deactivate nav buttons
    document.querySelectorAll('.nav-btn').forEach(el => el.classList.remove('active'));

    // Show target
    document.getElementById(tabId).classList.add('active');

    // Activate button (Hack for simple matching)
    const btn = Array.from(document.querySelectorAll('.nav-btn')).find(b => b.getAttribute('onclick').includes(tabId));
    if (btn) btn.classList.add('active');
}

// =======================
// DASHBOARD COMMANDS
// =======================
function toggleSystem() {
    var action = isRunning ? "stop" : "start";
    ws.send(JSON.stringify({ type: "command", action: action }));
}

function sendSettings() {
    var tpd = parseInt(document.getElementById('tpdPayload').value);
    var dur = parseInt(document.getElementById('durPayload').value);
    var dir = currentDirection;

    var msg = {
        type: "settings",
        tpd: tpd,
        dur: dur,
        dir: dir
    };
    ws.send(JSON.stringify(msg));
}

// =======================
// WIFI MANAGER
// =======================
function scanNetworks() {
    const list = document.getElementById('wifiList');
    list.innerHTML = '<div class="empty-state"><i class="fa-solid fa-spinner fa-spin"></i> Scanning...</div>';

    // Call API (Async)
    fetch('/api/wifi-scan')
        .then(response => response.json())
        .then(data => {
            if (data.status === 'scanning') {
                // Poll for results
                setTimeout(fetchWifiList, 3000);
            }
        });
}

function fetchWifiList() {
    fetch('/api/wifi-list')
        .then(response => response.json())
        .then(data => {
            if (data.length === 0) {
                // Try again if empty result (scan might be slow)
                setTimeout(fetchWifiList, 1000);
                return;
            }
            renderWifiList(data);
        });
}

function renderWifiList(networks) {
    const list = document.getElementById('wifiList');
    list.innerHTML = '';

    networks.forEach(net => {
        const item = document.createElement('div');
        item.className = 'wifi-item ' + (net.secure ? 'secure' : 'open');
        item.innerHTML = `
            <span>${net.ssid}</span>
            <div>
                <span style="margin-right:10px; font-size:0.8rem">${net.rssi} dBm</span>
                <i class="fa-solid ${net.secure ? 'fa-lock' : 'fa-lock-open'}"></i>
            </div>
        `;
        item.onclick = () => selectNetwork(net.ssid);
        list.appendChild(item);
    });
}

function selectNetwork(ssid) {
    document.getElementById('wifiConnectCard').classList.remove('hidden');
    document.getElementById('wifiSSID').value = ssid;
    document.getElementById('wifiPass').focus();
}

function connectWifi() {
    const ssid = document.getElementById('wifiSSID').value;
    const pass = document.getElementById('wifiPass').value;
    const btn = document.querySelector('#wifiConnectCard .btn-primary');

    btn.innerHTML = '<i class="fa-solid fa-spinner fa-spin"></i> Connecting...';

    // Post params
    const formData = new FormData();
    formData.append('ssid', ssid);
    formData.append('pass', pass);

    fetch('/api/wifi-connect', { method: 'POST', body: formData })
        .then(res => res.json())
        .then(data => {
            alert('Connection started! If successful, device IP might change.');
            btn.innerHTML = 'Connect';
            document.getElementById('wifiConnectCard').classList.add('hidden');
        });
}

// =======================
// DEVICE MANAGER (ESP-NOW)
// =======================
function scanPeers() {
    document.getElementById('deviceList').innerHTML = '<div class="empty-state"><i class="fa-solid fa-spinner fa-spin"></i> Discovering peers...</div>';
    // Send discovery command to Firmware
    ws.send(JSON.stringify({ type: "check_peers" }));
    // Note: Firmware should auto-broadcast discovery packets
}

function renderDeviceList(peers) {
    const list = document.getElementById('deviceList');
    list.innerHTML = '';

    if (!peers || peers.length === 0) {
        list.innerHTML = '<div class="empty-state">No peers found.</div>';
        return;
    }

    peers.forEach(peer => {
        const item = document.createElement('div');
        item.className = 'device-item';
        item.innerHTML = `
            <div>
                <div style="font-weight:bold">${peer.name || 'Unknown Device'}</div>
                <div style="font-size:0.8rem; color:#aaa">${peer.mac}</div>
            </div>
            <div>
                <button class="btn-icon" onclick="controlPeer('${peer.mac}', 'start')"><i class="fa-solid fa-play"></i></button>
                <button class="btn-icon" style="background:#ff4444" onclick="controlPeer('${peer.mac}', 'stop')"><i class="fa-solid fa-stop"></i></button>
            </div>
        `;
        list.appendChild(item);
    });
}

function controlPeer(mac, action) {
    // Forward command to specific peer
    ws.send(JSON.stringify({
        type: "peer_command",
        target: mac,
        action: action
    }));
}

// =======================
// UI HELPERS
// =======================
function updateTpdDisplay(val) { document.getElementById('tpdValue').innerText = val; }
function updateDurDisplay(val) { document.getElementById('durValue').innerText = val; }

function setDirection(dir) {
    currentDirection = dir;
    updateDirectionUI();
}

function updateDirectionUI() {
    document.getElementById('btn-cw').className = 'dir-btn';
    document.getElementById('btn-ccw').className = 'dir-btn';
    document.getElementById('btn-bi').className = 'dir-btn';

    if (currentDirection == 0) document.getElementById('btn-cw').classList.add('active');
    if (currentDirection == 1) document.getElementById('btn-ccw').classList.add('active');
    if (currentDirection == 2) document.getElementById('btn-bi').classList.add('active');
}

function updateStatusUI() {
    var statusText = document.getElementById('statusText');
    var toggleBtn = document.getElementById('toggleBtn');
    var toggleBtnText = document.getElementById('toggleBtnText');
    const lang = document.getElementById('languageSelect').value;
    const t = translations[lang] || translations['en'];

    if (isRunning) {
        statusText.innerText = t['running'];
        statusText.style.color = '#00ff88';
        toggleBtnText.innerText = t['stop'];
        toggleBtn.className = 'btn-large stop';
    } else {
        statusText.innerText = t['stopped'];
        statusText.style.color = '#ff4444';
        toggleBtnText.innerText = t['start'];
        toggleBtn.className = 'btn-large start';
    }
}

// =======================
// LOCALIZATION
// =======================
function changeLanguage(lang) {
    localStorage.setItem('selectedLanguage', lang);
    const t = translations[lang] || translations['en'];
    document.querySelectorAll('[data-i18n]').forEach(elem => {
        const key = elem.getAttribute('data-i18n');
        if (t[key]) elem.innerText = t[key];
    });
    updateStatusUI();
}

// =======================
// OTA AUTO UPDATE
// =======================
function triggerAutoUpdate() {
    if (!confirm("Check for updates and install if available? Device will reboot.")) return;

    const btn = document.querySelector('#settings button.btn-secondary'); // Target the update button
    const originalText = btn.innerHTML;

    btn.innerHTML = '<i class="fa-solid fa-spinner fa-spin"></i> Checking...';
    btn.disabled = true;

    fetch('/api/ota-auto', { method: 'POST' })
        .then(response => {
            if (response.ok) {
                alert('Update process started! The device will check version, download, and reboot if a new version is found. Please wait ~2 minutes.');
            } else {
                alert('Failed to start update process.');
                btn.innerHTML = originalText;
                btn.disabled = false;
            }
        })
        .catch(err => {
            console.error(err);
            alert('Error triggering update.');
            btn.innerHTML = originalText;
            btn.disabled = false;
        });
}
