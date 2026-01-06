var socket;
var isRunning = false;
var currentDirection = 2; // 0: CW, 1: CCW, 2: Bi-Directional
var wifiScanInterval;
var otaStatusInterval;
var deviceSuffix = ""; // WebSocket'ten gelecek

// Dil dosyasından çeviri al
function getTrans(key) {
    if (typeof translations !== 'undefined' && translations[currentLang] && translations[currentLang][key]) {
        return translations[currentLang][key];
    }
    return key;
}

window.onload = function () {
    initWebSocket();

    // Varsayılan Dil Kontrolü
    var userLang = navigator.language || navigator.userLanguage;
    var savedLang = localStorage.getItem('horus_lang');

    if (savedLang) {
        currentLang = savedLang;
    } else {
        // Tarayıcı dili destekleniyorsa onu seç, yoksa TR yap
        var langCode = userLang.split('-')[0];
        if (translations[langCode]) {
            currentLang = langCode;
        } else {
            currentLang = 'tr';
        }
    }
    document.getElementById('languageSelect').value = currentLang;
    applyLanguage(currentLang);

    // Tema Yükleme
    var savedTheme = localStorage.getItem('horus_theme') || 'auto';
    setTheme(savedTheme);

    // Renk Yükleme
    var savedColor = localStorage.getItem('horus_accent_color') || '#ffd700';
    setAccentColor(savedColor);

    // Check Version
    fetch('/api/version')
        .then(response => response.json())
        .then(data => {
            if (data.version) {
                document.getElementById('fwVersion').innerText = data.version;
            }
        }).catch(e => console.log(e));
};

function initWebSocket() {
    socket = new WebSocket('ws://' + window.location.hostname + '/ws');

    // Otomatik durum güncellemesi (10 saniyede bir)
    setInterval(function () {
        if (socket && socket.readyState === WebSocket.OPEN && !wifiScanInterval) {
            socket.send(JSON.stringify({ type: "check_peers" }));
        }
    }, 10000);

    socket.onopen = function () {
        console.log('WebSocket Connected');
        document.getElementById('connectionStatus').style.backgroundColor = '#0f0';
    };

    socket.onmessage = function (event) {
        var data = JSON.parse(event.data);
        console.log("WS Data:", data);

        if (data.running !== undefined) {
            isRunning = data.running;
            updateStatusUI();
        }

        if (data.tpd !== undefined) {
            document.getElementById('tpdPayload').value = data.tpd;
            document.getElementById('tpdValue').innerText = data.tpd;
        }
        if (data.dur !== undefined) {
            document.getElementById('durPayload').value = data.dur;
            document.getElementById('durValue').innerText = data.dur;
        }
        if (data.dir !== undefined) {
            setDirectionUI(data.dir);
        }
        if (data.name !== undefined) {
            document.getElementById('deviceName').value = data.name;
        }
        if (data.suffix !== undefined) {
            deviceSuffix = data.suffix;
        }

        if (data.espnow !== undefined) {
            document.getElementById('espNowToggle').checked = data.espnow;
        }

        if (data.peers) {
            renderPeers(data.peers);
        }

        if (data.type === "error") {
            alert(data.message);
        }

        if (data.type === "wifi_scan_error") {
            if (wifiScanInterval) {
                clearTimeout(wifiScanInterval);
                wifiScanInterval = null;
            }
            document.getElementById('wifiList').innerHTML = '<div class="list-item placeholder" style="color:#f66;">WiFi taraması zaman aşımına uğradı.</div>';
        }
    };

    socket.onclose = function () {
        console.log('WebSocket Disconnected');
        document.getElementById('connectionStatus').style.backgroundColor = '#f00';
        setTimeout(initWebSocket, 2000);
    };
}

function updateStatusUI() {
    var statusText = document.getElementById('statusText');
    var toggleBtn = document.getElementById('toggleBtn');

    if (isRunning) {
        statusText.innerText = getTrans('running');
        statusText.className = "status-text status-active";
        toggleBtn.innerText = getTrans('stop');
        toggleBtn.className = "btn-large btn-stop";
    } else {
        statusText.innerText = getTrans('paused');
        statusText.className = "status-text";
        toggleBtn.innerText = getTrans('start');
        toggleBtn.className = "btn-large";
    }
}

function toggleSystem() {
    var action = isRunning ? "stop" : "start";
    var cmd = { type: "command", action: action };
    socket.send(JSON.stringify(cmd));
}

function toggleEspNow() {
    var isEnabled = document.getElementById('espNowToggle').checked;
    var settings = {
        type: "settings",
        espnow: isEnabled
    };
    socket.send(JSON.stringify(settings));
}

function updateSliderLabels() {
    document.getElementById('tpdValue').innerText = document.getElementById('tpdPayload').value;
    document.getElementById('durValue').innerText = document.getElementById('durPayload').value;
}

function setDirection(dir) {
    currentDirection = dir;
    setDirectionUI(dir);
}

function setDirectionUI(dir) {
    currentDirection = dir;
    document.getElementById('dirCW').className = "dir-btn" + (dir == 0 ? " active" : "");
    document.getElementById('dirCCW').className = "dir-btn" + (dir == 1 ? " active" : "");
    document.getElementById('dirBi').className = "dir-btn" + (dir == 2 ? " active" : "");
}

function sendSettings() {
    var tpd = parseInt(document.getElementById('tpdPayload').value);
    var dur = parseInt(document.getElementById('durPayload').value);
    var dir = currentDirection;

    var settings = {
        type: "settings",
        tpd: tpd,
        dur: dur,
        dir: dir
    };
    socket.send(JSON.stringify(settings));
    alert(getTrans('saved'));
}

function saveDeviceName() {
    var name = document.getElementById('deviceName').value;

    if (!name || name.trim() === '') {
        alert('Lütfen geçerli bir cihaz adı girin.');
        return;
    }

    var settings = {
        type: "settings",
        name: name
    };

    socket.send(JSON.stringify(settings));

    // Kullanıcıya bilgi ver
    alert('Cihaz adı kaydedildi. Cihaz yeniden başlatılıyor...');

    // 3 saniye bekle, sonra yeni hostname'e yönlendir
    setTimeout(function () {
        // Slugify fonksiyonunu JavaScript'te de uyguluyoruz
        var slugifiedName = name.toLowerCase()
            .replace(/ş/g, 's').replace(/Ş/g, 's')
            .replace(/ı/g, 'i').replace(/İ/g, 'i')
            .replace(/ğ/g, 'g').replace(/Ğ/g, 'g')
            .replace(/ü/g, 'u').replace(/Ü/g, 'u')
            .replace(/ö/g, 'o').replace(/Ö/g, 'o')
            .replace(/ç/g, 'c').replace(/Ç/g, 'c')
            .replace(/[^a-z0-9]/g, '-')
            .replace(/--+/g, '-')
            .replace(/^-|-$/g, '');

        if (!slugifiedName) slugifiedName = 'horus';

        // Hostname: slugName + "-" + suffix + ".local"
        // Suffix yoksa (hata veya eski FW) sadece slugName kullan (veya varsayılan suffix)
        var newHostname = slugifiedName;
        if (deviceSuffix) {
            newHostname += "-" + deviceSuffix;
        }

        // Yeni hostname'e yönlendir
        window.location.href = 'http://' + newHostname + '.local';
    }, 3000);
}

// ================= TAB LOGIC =================
function switchTab(tabId) {
    // Hide all contents
    var contents = document.getElementsByClassName('tab-content');
    for (var i = 0; i < contents.length; i++) {
        contents[i].classList.remove('active');
    }

    // Deactivate all nav buttons
    var navs = document.getElementsByClassName('nav-btn');
    for (var i = 0; i < navs.length; i++) {
        navs[i].classList.remove('active');
    }

    // Activate target
    document.getElementById(tabId).classList.add('active');
    document.getElementById('nav-' + tabId).classList.add('active');
}

// ================= DEVICE DISCOVERY & CONTROL =================
function refreshPeers() {
    var cmd = { type: "check_peers" };
    socket.send(JSON.stringify(cmd));
    // Placeholder kaldırıldı - akıcı güncelleme için
}

function renderPeers(peers) {
    var list = document.getElementById('deviceList');
    list.innerHTML = "";
    if (peers.length == 0) {
        list.innerHTML = '<div class="list-item placeholder" data-i18n="no_devices">' + getTrans('no_devices') + '</div>';
        return;
    }

    peers.forEach(function (p) {
        // Varsayılan değerler
        var tpd = p.tpd !== undefined ? p.tpd : 900;
        var dur = p.dur !== undefined ? p.dur : 10;
        var dir = p.dir !== undefined ? p.dir : 2;
        var isRunning = p.running === true;
        var isOnline = (p.online !== undefined) ? p.online : true;

        var div = document.createElement('div');
        div.className = 'card peer-card' + (isOnline ? '' : ' offline');
        div.id = 'peer-' + p.mac;

        var statusColor = isOnline ? (isRunning ? 'var(--success-color)' : 'var(--text-secondary)') : '#555';

        // Kart İçeriği
        var html = `
            <div class="peer-header">
                <div>
                    <h3>${p.name}</h3>
                    <small style="opacity:0.6">${p.mac}</small>
                </div>
                <div style="display:flex; align-items:center; gap:10px;">
                    <div style="width:12px; height:12px; border-radius:50%; background:${statusColor}; box-shadow: 0 0 5px ${statusColor};" title="${isOnline ? 'Online' : 'Offline'}"></div>
                    <button class="btn-icon" onclick="deletePeer('${p.mac}')" title="${getTrans('delete') || 'Sil'}">
                        <svg style="width:20px;height:20px" viewBox="0 0 24 24"><path fill="currentColor" d="M19,4H15.5L14.5,3H9.5L8.5,4H5V6H19M6,19A2,2 0 0,0 8,21H16A2,2 0 0,0 18,19V7H6V19Z" /></svg>
                    </button>
                </div>
            </div>
            
            <div class="peer-controls-grid">
                <div class="control-group">
                    <label>TPD</label>
                    <input type="number" id="p-tpd-${p.mac}" value="${tpd}">
                </div>
                <div class="control-group">
                    <label>${getTrans('duration') || 'Süre'}</label>
                    <input type="number" id="p-dur-${p.mac}" value="${dur}">
                </div>
            </div>
            
            <div class="control-group">
                <label>${getTrans('direction') || 'Yön'}</label>
                <div class="direction-selector">
                    <button class="dir-btn ${dir == 0 ? 'active' : ''}" onclick="setPeerDirUI(this, '${p.mac}', 0)">CW</button>
                    <button class="dir-btn ${dir == 1 ? 'active' : ''}" onclick="setPeerDirUI(this, '${p.mac}', 1)">CCW</button>
                    <button class="dir-btn ${dir == 2 ? 'active' : ''}" onclick="setPeerDirUI(this, '${p.mac}', 2)">⇄</button>
                </div>
                <input type="hidden" id="p-dir-${p.mac}" value="${dir}">
            </div>
            
            <div class="peer-actions" style="display:flex; gap:10px; margin-top:15px;">
                <button class="btn-small ${isRunning ? 'btn-stop' : ''}" style="flex:1" onclick="togglePeer('${p.mac}', ${!isRunning})">
                    ${isRunning ? (getTrans('stop') || 'DURDUR') : (getTrans('start') || 'BAŞLAT')}
                </button>
                <button class="btn-secondary" style="flex:1" onclick="pushPeerSettings('${p.mac}', ${isRunning})">
                    ${getTrans('save') || 'KAYDET'}
                </button>
            </div>
        `;

        div.innerHTML = html;
        list.appendChild(div);
    });
}
// Global scope functions for onclick
window.setPeerDirUI = function (btn, mac, dir) {
    var parent = btn.parentElement;
    var btns = parent.getElementsByClassName('dir-btn');
    for (var i = 0; i < btns.length; i++) btns[i].classList.remove('active');
    btn.classList.add('active');
    document.getElementById('p-dir-' + mac).value = dir;
};

window.pushPeerSettings = function (mac, currentRunningState) {
    var tpd = parseInt(document.getElementById('p-tpd-' + mac).value);
    var dur = parseInt(document.getElementById('p-dur-' + mac).value);
    var dir = parseInt(document.getElementById('p-dir-' + mac).value);

    // Running state'i inputtan değil, fonksiyona gelen mevcut parametreden al ama 
    // togglePeer ayrı çalışıyor. Buradaki amaç ayarları kaydetmek. 
    // Ayar kaydederken motoru durdurmasın veya başlatmasın, mevcut durumu korusun
    // ANCAK: Kullanıcı başlat/durdur butonuna basmadan ayar gönderirse running ne olacak?
    // renderPeers'ta running state'i güncellemiştik.

    var cmd = {
        type: "peer_settings",
        target: mac,
        tpd: tpd,
        dur: dur,
        dir: dir,
        running: currentRunningState // Statusu koru
    };

    socket.send(JSON.stringify(cmd));
    // Görsel geri bildirim
    alert('Ayarlar cihaza gönderildi!');
};

window.togglePeer = function (mac, newState) {
    // Mevcut input değerlerini de alalım ki toggle yaparken ayarlar sıfırlanmasın
    var tpd = parseInt(document.getElementById('p-tpd-' + mac).value);
    var dur = parseInt(document.getElementById('p-dur-' + mac).value);
    var dir = parseInt(document.getElementById('p-dir-' + mac).value);

    var cmd = {
        type: "peer_settings",
        target: mac,
        tpd: tpd,
        dur: dur,
        dir: dir,
        running: newState
    };
    socket.send(JSON.stringify(cmd));
};

window.deletePeer = function (mac) {
    var msg = (typeof getTrans === 'function' && getTrans('confirm_delete')) ? getTrans('confirm_delete') : "Cihazı silmek istediğinize emin misiniz?";
    if (confirm(msg)) {
        var cmd = {
            type: "del_peer",
            target: mac
        };
        socket.send(JSON.stringify(cmd));
    }
};

// ================= WIFI LOGIC =================
function scanWifi() {
    var list = document.getElementById('wifiList');
    list.innerHTML = '<div class="list-item placeholder" data-i18n="scanning">' + getTrans('scanning') + '</div>';

    fetch('/api/wifi-scan')
        .then(response => response.json())
        .then(data => {
            if (data.status == "scanning") {
                checkWifiScanResult();
            } else {
                // busy
                setTimeout(checkWifiScanResult, 2000);
            }
        });
}

function checkWifiScanResult() {
    fetch('/api/wifi-list')
        .then(response => response.json())
        .then(data => {
            if (data.length == 0 && !wifiScanInterval) {
                // Still scanning or empty
                if (!wifiScanInterval)
                    wifiScanInterval = setTimeout(checkWifiScanResult, 2000);
            } else {
                renderWifiList(data);
                wifiScanInterval = null;
            }
        });
}

function renderWifiList(networks) {
    var list = document.getElementById('wifiList');
    list.innerHTML = "";
    if (networks.length == 0) {
        list.innerHTML = '<div class="list-item placeholder" data-i18n="no_networks">' + getTrans('no_networks') + '</div>';
        return;
    }

    networks.forEach(function (net) {
        var item = document.createElement('div');
        item.className = "list-item";
        item.innerHTML = `
             <div class="list-info">
                <span class="ssid">${net.ssid}</span>
                <span class="rssi">Signal: ${net.rssi} dBm</span>
            </div>
            <div class="list-action">
                <button class="btn-small" onclick="openWifiModal('${net.ssid}')" data-i18n="connect">${getTrans('connect')}</button>
            </div>
        `;
        list.appendChild(item);
    });
}

function openWifiModal(ssid) {
    document.getElementById('modalSSID').innerText = ssid;
    document.getElementById('wifiModal').style.display = 'block';

    // Focus password field for usability
    setTimeout(() => document.getElementById('wifiPass').focus(), 100);
}

function closeWifiModal() {
    document.getElementById('wifiModal').style.display = 'none';
    document.getElementById('wifiPass').value = '';
}

// Close modal if clicking outside
window.onclick = function (event) {
    if (event.target == document.getElementById('wifiModal')) {
        closeWifiModal();
    }
}

function connectWifi() {
    var ssid = document.getElementById('modalSSID').innerText;
    var pass = document.getElementById('wifiPass').value;

    var formData = new FormData();
    formData.append('ssid', ssid);
    formData.append('pass', pass);

    fetch('/api/wifi-connect', { method: 'POST', body: formData })
        .then(response => response.json())
        .then(data => {
            closeWifiModal();
            if (data.status == "started") {
                if (confirm(getTrans('connection_started') + " Reload?")) {
                    location.reload();
                }
            } else {
                alert("Error connecting.");
            }
        });
}

// ================= OTA LOGIC =================
function triggerAutoUpdate() {
    if (!confirm(getTrans('confirm_update') || "Update firmware?")) return;

    fetch('/api/ota-auto', { method: 'POST' })
        .then(response => response.json())
        .then(data => {
            if (data.status == "started" || data.status == "updating") {
                alert("Update started! Please wait...");
                if (!otaStatusInterval) {
                    otaStatusInterval = setInterval(checkOtaStatus, 2000);
                }
            } else if (data.status == "busy") {
                alert("Update already in progress.");
            }
        })
        .catch(e => alert("Error triggering update"));
}

function checkOtaStatus() {
    fetch('/api/ota-status')
        .then(response => response.json())
        .then(data => {
            console.log("OTA Status:", data.status);
            var statusDiv = document.getElementById('statusText'); // Reuse main status for feedback?
            // Or better, just alert user or use a toast. 
            // For now, let's just log and stop if done

            if (data.status == "up_to_date") {
                clearInterval(otaStatusInterval);
                otaStatusInterval = null;
                alert("System is already up to date.");
            } else if (data.status == "error") {
                clearInterval(otaStatusInterval);
                otaStatusInterval = null;
                alert("Update failed!");
            } else if (data.status == "idle") {
                // finished successfully (rebooted) or not started
                clearInterval(otaStatusInterval);
                otaStatusInterval = null;
                // Likely rebooted if it was updating
            }
        });
}

function changeLanguage() {
    var sel = document.getElementById('languageSelect');
    currentLang = sel.value;
    localStorage.setItem('horus_lang', currentLang);
    applyLanguage(currentLang);
}

function applyLanguage(lang) {
    var t = translations[lang];
    if (!t) t = translations['tr']; // fallback

    document.querySelectorAll('[data-i18n]').forEach(el => {
        var key = el.getAttribute('data-i18n');
        if (t[key]) {
            el.innerText = t[key];
        }
    });

    // Update dynamic texts
    updateStatusUI();
}

// ================= THEME FUNCTIONS =================
function setTheme(theme) {
    localStorage.setItem('horus_theme', theme);
    document.documentElement.setAttribute('data-theme', theme);

    // Update theme buttons
    document.querySelectorAll('.theme-btn').forEach(btn => {
        btn.classList.remove('active');
        if (btn.getAttribute('data-theme') === theme) {
            btn.classList.add('active');
        }
    });
}

function setAccentColor(color) {
    localStorage.setItem('horus_accent_color', color);
    document.documentElement.style.setProperty('--accent-color', color);

    // Update color buttons
    document.querySelectorAll('.color-btn').forEach(btn => {
        btn.classList.remove('active');
        if (btn.getAttribute('data-color') === color) {
            btn.classList.add('active');
        }
    });
}
