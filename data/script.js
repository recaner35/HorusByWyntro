var currentLang = 'tr';
var socket;
var isRunning = false;
var currentDirection = 2; 
var otaStatusInterval;
var statusInterval;
var deviceSuffix = ""; 
var wifiScanInterval; // Eski değişkenleri geri koydum hata almasın diye

// Dil Fonksiyonu
function getTrans(key) {
    if (typeof translations !== 'undefined' && translations[currentLang] && translations[currentLang][key]) {
        return translations[currentLang][key];
    }
    return key;
}

window.onload = function () {
    initWebSocket();
    
    // Captive Portal / Kurulum Ekranı Tetikleyici
    if (location.hostname === "192.168.4.1" || location.hostname === "horus" || location.hostname.toLowerCase().includes("horus")) {
        var setupModal = document.getElementById('setupModal');
        if(setupModal) {
            setupModal.style.display = 'flex';
            setTimeout(() => {
                var devInput = document.getElementById('setupDeviceName');
                if(devInput && devInput.value === "") {
                     devInput.value = "Horus-" + deviceSuffix;
                }
            }, 1000);
        }
    }

    // Dil ve Tema Ayarları
    var userLang = navigator.language || navigator.userLanguage;
    var savedLang = localStorage.getItem('horus_lang');
    if (savedLang) {
        currentLang = savedLang;
    } else {
        var langCode = userLang.split('-')[0];
        if (typeof translations !== 'undefined' && translations[langCode]) {
            currentLang = langCode;
        } else {
            currentLang = 'tr';
        }
    }
    var langSelect = document.getElementById('languageSelect');
    if(langSelect) langSelect.value = currentLang;
    applyLanguage(currentLang);

    var savedTheme = localStorage.getItem('horus_theme') || 'auto';
    setTheme(savedTheme);

    var savedColor = localStorage.getItem('horus_accent_color') || '#fdcb6e';
    setAccentColor(savedColor);

    // Versiyon Kontrol
    fetch('/api/version')
        .then(response => response.json())
        .then(data => {
            if (data.version && document.getElementById('fwVersion')) {
                document.getElementById('fwVersion').innerText = data.version;
            }
        }).catch(e => console.log(e));
};

function initWebSocket() {
    var protocol = location.protocol === 'https:' ? 'wss://' : 'ws://';
    socket = new WebSocket(protocol + location.host + '/ws');

    if (statusInterval) clearInterval(statusInterval);
    statusInterval = setInterval(function () {
        if (socket && socket.readyState === WebSocket.OPEN) {
            socket.send(JSON.stringify({ type: "check_peers" }));
        }
    }, 10000);

    socket.onopen = function () {
        console.log('WebSocket Connected');
        var statusEl = document.getElementById('connectionStatus');
        if(statusEl) statusEl.style.backgroundColor = '#0f0';
    };

    socket.onmessage = function (event) {
        var data;
        try {
            data = JSON.parse(event.data);
        } catch (e) { return; }
        
        if (data.running !== undefined) { isRunning = data.running; updateStatusUI(); }
        if (data.tpd !== undefined) {
            if(document.getElementById('tpdPayload')) document.getElementById('tpdPayload').value = data.tpd;
            if(document.getElementById('tpdValue')) document.getElementById('tpdValue').innerText = data.tpd;
        }
        if (data.dur !== undefined) {
            if(document.getElementById('durPayload')) document.getElementById('durPayload').value = data.dur;
            if(document.getElementById('durValue')) document.getElementById('durValue').innerText = data.dur;
        }
        if (data.dir !== undefined) setDirectionUI(data.dir);
        if (data.name !== undefined && document.getElementById('deviceName')) document.getElementById('deviceName').value = data.name;
        if (data.suffix !== undefined) deviceSuffix = data.suffix;
        if (data.espnow !== undefined && document.getElementById('espNowToggle')) document.getElementById('espNowToggle').checked = data.espnow;
        if (data.peers) renderPeers(data.peers);
        if (data.type === "error") showToast(data.message, "error");
    };

    socket.onclose = function () {
        var statusEl = document.getElementById('connectionStatus');
        if(statusEl) statusEl.style.backgroundColor = '#f00';
        setTimeout(initWebSocket, 2000);
    };
}

function updateStatusUI() {
    var statusText = document.getElementById('statusText');
    var toggleBtn = document.getElementById('toggleBtn');
    if(!statusText || !toggleBtn) return;

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
    if (socket && socket.readyState === WebSocket.OPEN) socket.send(JSON.stringify({ type: "command", action: action }));
}

function toggleEspNow() {
    var isEnabled = document.getElementById('espNowToggle').checked;
    if (socket && socket.readyState === WebSocket.OPEN) socket.send(JSON.stringify({ type: "settings", espnow: isEnabled }));
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
    var btnCW = document.getElementById('dirCW');
    var btnCCW = document.getElementById('dirCCW');
    var btnBi = document.getElementById('dirBi');
    if(btnCW) btnCW.className = "dir-btn" + (dir == 0 ? " active" : "");
    if(btnCCW) btnCCW.className = "dir-btn" + (dir == 1 ? " active" : "");
    if(btnBi) btnBi.className = "dir-btn" + (dir == 2 ? " active" : "");
}

function sendSettings() {
    var tpd = parseInt(document.getElementById('tpdPayload').value);
    var dur = parseInt(document.getElementById('durPayload').value);
    var dir = currentDirection;
    socket.send(JSON.stringify({ type: "settings", tpd: tpd, dur: dur, dir: dir }));
    showToast(getTrans('saved') || "Kaydedildi");
}

function saveDeviceName() {
    var name = document.getElementById('deviceName').value;
    if (!name || name.trim() === '') { showToast('Lütfen geçerli bir cihaz adı girin.'); return; }
    socket.send(JSON.stringify({ type: "settings", name: name }));
    showToast('Cihaz adı kaydedildi. Cihaz yeniden başlatılıyor...');
    
    // Basit Slugify
    var slugName = name.toLowerCase().replace(/[^a-z0-9]/g, '-').replace(/-+/g, '-').replace(/^-|-$/g, '');
    if(!slugName) slugName = 'horus';
    setTimeout(function () {
        window.location.href = 'http://' + slugName + "-" + deviceSuffix + '.local';
    }, 3000);
}

function switchTab(tabId) {
    var contents = document.getElementsByClassName('tab-content');
    for (var i = 0; i < contents.length; i++) contents[i].classList.remove('active');
    var navs = document.getElementsByClassName('nav-btn');
    for (var i = 0; i < navs.length; i++) navs[i].classList.remove('active');
    document.getElementById(tabId).classList.add('active');
    document.getElementById('nav-' + tabId).classList.add('active');
}

function refreshPeers() {
    if (socket && socket.readyState === WebSocket.OPEN) socket.send(JSON.stringify({ type: "check_peers" }));
}
    
function renderPeers(peers) {
    var list = document.getElementById('deviceList');
    if(!list) return;
    list.innerHTML = "";
    if (peers.length == 0) {
        list.innerHTML = '<div class="list-item placeholder" data-i18n="no_devices">' + getTrans('no_devices') + '</div>';
        return;
    }
    peers.forEach(function (p) {
        var tpd = p.tpd !== undefined ? p.tpd : 900;
        var dur = p.dur !== undefined ? p.dur : 10;
        var isRunning = p.running === true;
        var isOnline = (p.online !== undefined) ? p.online : true;
        var div = document.createElement('div');
        div.className = 'card peer-card' + (isOnline ? '' : ' offline');
        div.id = 'peer-' + p.mac.replace(/[^a-zA-Z0-9]/g, '');
        var statusColor = isOnline ? (isRunning ? 'var(--success-color)' : 'var(--text-secondary)') : '#555';

        div.innerHTML = `
            <div class="peer-header">
                <div><h3>${p.name}</h3><small style="opacity:0.6">${p.mac}</small></div>
                <div style="display:flex; align-items:center; gap:10px;">
                    <div style="width:12px; height:12px; border-radius:50%; background:${statusColor};"></div>
                    <button class="btn-icon" onclick="deletePeer('${p.mac}')">🗑️</button>
                </div>
            </div>
            <div class="peer-controls-grid">
                <div class="control-group"><label>TPD: <span id="p-tpd-val-${p.mac}">${tpd}</span></label><input type="range" min="100" max="3000" step="10" value="${tpd}" id="p-tpd-${p.mac}" oninput="document.getElementById('p-tpd-val-${p.mac}').innerText = this.value"></div>
                <div class="control-group"><label>${getTrans('duration') || 'Süre'}: <span id="p-dur-val-${p.mac}">${dur}</span></label><input type="range" min="1" max="120" step="1" value="${dur}" id="p-dur-${p.mac}" oninput="document.getElementById('p-dur-val-${p.mac}').innerText = this.value"></div>
            </div>
            <div class="peer-actions" style="display:flex; gap:10px; margin-top:15px;">
                <button class="btn-small ${isRunning ? 'btn-stop' : ''}" style="flex:1" onclick="togglePeer('${p.mac}', ${!isRunning})">${isRunning ? (getTrans('stop') || 'DURDUR') : (getTrans('start') || 'BAŞLAT')}</button>
                <button class="btn-secondary" style="flex:1" onclick="pushPeerSettings('${p.mac}', ${isRunning})">${getTrans('save') || 'KAYDET'}</button>
            </div>`;
        list.appendChild(div);
    });
}

window.pushPeerSettings = function (mac, currentRunningState) {
    var tpd = parseInt(document.getElementById('p-tpd-' + mac).value);
    var dur = parseInt(document.getElementById('p-dur-' + mac).value);
    var cmd = { type: "peer_settings", target: mac, tpd: tpd, dur: dur, dir: 2, running: currentRunningState };
    if (socket && socket.readyState === WebSocket.OPEN) { socket.send(JSON.stringify(cmd)); showToast('Ayarlar kutuya gönderildi!'); }    
};

window.togglePeer = function (mac, newState) {
    var tpd = parseInt(document.getElementById('p-tpd-' + mac).value);
    var dur = parseInt(document.getElementById('p-dur-' + mac).value);
    var cmd = { type: "peer_settings", target: mac, tpd: tpd, dur: dur, dir: 2, running: newState };
    if (socket && socket.readyState === WebSocket.OPEN) socket.send(JSON.stringify(cmd));
};

window.deletePeer = function (mac) {
    if (confirm(getTrans('confirm_delete') || "Silinsin mi?")) {
        if (socket && socket.readyState === WebSocket.OPEN) socket.send(JSON.stringify({ type: "del_peer", target: mac }));
    }
};

// ================= WIFI LOGIC (DÜZELTİLEN KISIM) =================

// HTML'deki butonlar "scanWifi()" arıyor, o yüzden bu ismi geri getirdik!
function scanWifi() {
    var list = document.getElementById('wifiList');
    // Eğer Wifi Modal içindeki liste varsa orayı güncelle
    // Yoksa (ana sayfada basıldıysa) önce modalı aç
    var wifiModal = document.getElementById('wifiModal');
    if(wifiModal && wifiModal.style.display == 'none') {
        openWifiSettings(); // Modalı açar ve taramayı başlatır
    } else {
        // Zaten modal açıksa sadece taramayı yenile
        scanNetworks();
    }
}

// Modal açma fonksiyonu (Setup ekranından çağrılıyor)
function openWifiSettings() {
    var setupModal = document.getElementById('setupModal');
    if(setupModal) setupModal.style.display = 'none';

    var wifiModal = document.getElementById('wifiModal');
    if(wifiModal) {
        wifiModal.style.display = 'flex';
        scanNetworks(); // Modalı açınca hemen tara
    }
}

// Asıl tarama yapan fonksiyon
function scanNetworks() {
    var listDiv = document.getElementById('networkList');
    // Eğer setup modal içindeki liste yoksa, belki wifiModal içindeki listeye bakmalı
    if(!listDiv) listDiv = document.getElementById('wifiList'); // Uyumluluk için
    
    if(!listDiv) return;

    listDiv.innerHTML = `<div class="loader-container"><div class="loader"></div><p>${getTrans('scanning') || 'Taranıyor...'}</p></div>`;

    fetch('/scan')
        .then(response => response.json())
        .then(networks => {
            listDiv.innerHTML = ""; 
            if (networks.length === 0) {
                listDiv.innerHTML = "<div style='padding:15px; text-align:center;'>Ağ bulunamadı. <br><a href='#' onclick='scanNetworks()'>Tekrar Dene</a></div>";
                return;
            }
            networks.sort((a, b) => b.rssi - a.rssi);
            networks.forEach(net => {
                var item = document.createElement('div');
                item.className = 'network-item'; // CSS class
                // Eski CSS yapısına uyumlu olması için:
                if(!document.querySelector('.network-item')) item.className = 'list-item'; 

                var lockIcon = net.secure ? '🔒' : '';
                item.innerHTML = `<span class="ssid">${net.ssid}</span><span class="signal">${lockIcon} ${net.rssi} dBm</span>`;
                
                item.onclick = function() {
                    var ssidInput = document.getElementById('wifiSSID');
                    var passInput = document.getElementById('wifiPass');
                    if(ssidInput) ssidInput.value = net.ssid;
                    if(passInput) passInput.focus();
                    // Seçim efekti
                    var items = listDiv.children;
                    for(var i=0; i<items.length; i++) items[i].style.background = 'transparent';
                    item.style.background = 'rgba(253, 203, 110, 0.2)';
                };
                listDiv.appendChild(item);
            });
        })
        .catch(err => {
            listDiv.innerHTML = "<div style='padding:15px; text-align:center;'>Hata.<br><button class='btn-small' onclick='scanNetworks()'>Tekrar</button></div>";
        });
}

function saveAndConnect() {
    var ssid = document.getElementById('wifiSSID').value;
    var pass = document.getElementById('wifiPass').value;
    var nameInput = document.getElementById('setupDeviceName');
    var name = nameInput ? nameInput.value : "Horus"; 

    if(!ssid) { showToast("SSID Girin!", "error"); return; }
    showToast("Kaydediliyor...", "info");

    var formData = new FormData();
    formData.append("ssid", ssid);
    formData.append("pass", pass);
    formData.append("name", name);

    fetch('/save-wifi', { method: 'POST', body: formData })
    .then(response => {
        if (response.ok) {
            var wifiModal = document.getElementById('wifiModal');
            if(wifiModal) wifiModal.style.display = 'none';
            showToast("Kaydedildi! Yeniden başlatılıyor...", "success");
            
            document.body.innerHTML = `
                <div style="display:flex; flex-direction:column; align-items:center; justify-content:center; height:100vh; text-align:center; color:#fff; padding:20px;">
                    <h2>Yeniden Başlatılıyor</h2>
                    <p>Lütfen ev ağınıza bağlanın.</p>
                    <h3 style="color:var(--accent-color);">http://horus-${deviceSuffix}.local</h3>
                    <div class="loader"></div>
                </div>`;
            setTimeout(() => { window.location.href = "http://horus-" + deviceSuffix + ".local"; }, 15000);
        } else { showToast("Hata!", "error"); }
    })
    .catch(error => { showToast("Bağlantı kesiliyor...", "info"); });
}

function skipSetup() {
    var target = "http://horus-" + deviceSuffix + ".local";
    var setupModal = document.getElementById('setupModal');
    if(setupModal) setupModal.style.display = 'none';
    showToast("Yönlendiriliyor...", "info");
    setTimeout(() => { window.location.href = target; }, 1500);
}

// OTA
function triggerAutoUpdate() {
    if (!confirm(getTrans('confirm_update'))) return;
    fetch('/api/ota-auto', { method: 'POST' }).then(r => r.json()).then(d => showToast("Güncelleme başladı")).catch(e => showToast("Hata"));
}

// Dil/Tema Yardımcıları
function changeLanguage() { var sel = document.getElementById('languageSelect'); currentLang = sel.value; localStorage.setItem('horus_lang', currentLang); applyLanguage(currentLang); }
function applyLanguage(lang) {
    if(typeof translations === 'undefined') return;
    var t = translations[lang] || translations['tr']; 
    document.querySelectorAll('[data-i18n]').forEach(el => { var key = el.getAttribute('data-i18n'); if (t[key]) el.innerText = t[key]; });
    updateStatusUI();
}
function setTheme(theme) { localStorage.setItem('horus_theme', theme); document.documentElement.setAttribute('data-theme', theme); document.querySelectorAll('.theme-btn').forEach(btn => { btn.classList.remove('active'); if (btn.getAttribute('data-theme') === theme) btn.classList.add('active'); }); }
function setAccentColor(color) { localStorage.setItem('horus_accent_color', color); document.documentElement.style.setProperty('--accent-color', color); document.querySelectorAll('.color-btn').forEach(btn => { btn.classList.remove('active'); if (btn.getAttribute('data-color') === color) btn.classList.add('active'); }); }
function showToast(message, type = "info") {
    const container = document.getElementById("toast-container");
    if (!container) return;
    const toast = document.createElement("div");
    toast.className = `toast ${type}`;
    toast.textContent = message;
    container.appendChild(toast);
    setTimeout(() => { toast.style.opacity = "0"; toast.style.transform = "translateY(10px)"; setTimeout(() => toast.remove(), 300); }, 2500);
}
