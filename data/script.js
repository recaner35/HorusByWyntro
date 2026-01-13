var currentLang = 'tr';
var socket;
var isRunning = false;
var currentDirection = 2; // 0: CW, 1: CCW, 2: Bi-Directional
var otaStatusInterval;
var statusInterval;
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
    
    // Setup/Captive Portal Kontrolü
    // Eğer IP 192.168.4.1 ise veya hostname "Horus" ise kurulum ekranını aç
    if (location.hostname === "192.168.4.1" || location.hostname === "horus" || location.hostname === "Horus") {
        var setupModal = document.getElementById('setupModal');
        if(setupModal) {
            setupModal.style.display = 'flex';
            // Cihaz adını WebSocket'ten gelmesini beklemeden varsayılanı doldurmaya çalışalım
            setTimeout(() => {
                var devInput = document.getElementById('setupDeviceName');
                if(devInput && devInput.value === "") {
                     devInput.value = "Horus-" + deviceSuffix;
                }
            }, 1000);
        }
    }

    // Varsayılan Dil Kontrolü
    var userLang = navigator.language || navigator.userLanguage;
    var savedLang = localStorage.getItem('horus_lang');

    if (savedLang) {
        currentLang = savedLang;
    } else {
        var langCode = userLang.split('-')[0];
        if (translations && translations[langCode]) {
            currentLang = langCode;
        } else {
            currentLang = 'tr';
        }
    }
    
    var langSelect = document.getElementById('languageSelect');
    if(langSelect) langSelect.value = currentLang;
    applyLanguage(currentLang);

    // Tema Yükleme
    var savedTheme = localStorage.getItem('horus_theme') || 'auto';
    setTheme(savedTheme);

    // Renk Yükleme
    var savedColor = localStorage.getItem('horus_accent_color') || '#fdcb6e';
    setAccentColor(savedColor);

    // Check Version
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

    // Otomatik durum güncellemesi (10 saniyede bir)
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
        } catch (e) {
            console.error("Bozuk WS verisi:", event.data);
        return;
        }
        
        if (data.running !== undefined) {
            isRunning = data.running;
            updateStatusUI();
        }
        if (data.tpd !== undefined) {
            if(document.getElementById('tpdPayload')) document.getElementById('tpdPayload').value = data.tpd;
            if(document.getElementById('tpdValue')) document.getElementById('tpdValue').innerText = data.tpd;
        }
        if (data.dur !== undefined) {
            if(document.getElementById('durPayload')) document.getElementById('durPayload').value = data.dur;
            if(document.getElementById('durValue')) document.getElementById('durValue').innerText = data.dur;
        }
        if (data.dir !== undefined) {
            setDirectionUI(data.dir);
        }
        if (data.name !== undefined) {
            if(document.getElementById('deviceName')) document.getElementById('deviceName').value = data.name;
        }
        if (data.suffix !== undefined) {
            deviceSuffix = data.suffix;
        }

        if (data.espnow !== undefined) {
            if(document.getElementById('espNowToggle')) document.getElementById('espNowToggle').checked = data.espnow;
        }

        if (data.peers) {
            renderPeers(data.peers);
        }

        if (data.type === "error") {
            showToast(data.message, "error");
        }
    };

    socket.onclose = function () {
        console.log('WebSocket Disconnected');
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
    var cmd = { type: "command", action: action };
    if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send(JSON.stringify(cmd));
    }
}

function toggleEspNow() {
    var isEnabled = document.getElementById('espNowToggle').checked;
    var settings = {
        type: "settings",
        espnow: isEnabled
    };
    if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send(JSON.stringify(settings));
    }
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

    var settings = {
        type: "settings",
        tpd: tpd,
        dur: dur,
        dir: dir
    };
    socket.send(JSON.stringify(settings));
    showToast(getTrans('saved') || "Kaydedildi");
}

function saveDeviceName() {
    var name = document.getElementById('deviceName').value;

    if (!name || name.trim() === '') {
       showToast('Lütfen geçerli bir cihaz adı girin.');
       return;
    }

    var settings = {
        type: "settings",
        name: name
    };

    socket.send(JSON.stringify(settings));

    showToast('Cihaz adı kaydedildi. Cihaz yeniden başlatılıyor...');

    setTimeout(function () {
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

        var newHostname = slugifiedName;
        if (deviceSuffix) {
            newHostname += "-" + deviceSuffix;
        }

        window.location.href = 'http://' + newHostname + '.local';
    }, 3000);
}

// ================= TAB LOGIC =================
function switchTab(tabId) {
    var contents = document.getElementsByClassName('tab-content');
    for (var i = 0; i < contents.length; i++) {
        contents[i].classList.remove('active');
    }

    var navs = document.getElementsByClassName('nav-btn');
    for (var i = 0; i < navs.length; i++) {
        navs[i].classList.remove('active');
    }

    document.getElementById(tabId).classList.add('active');
    document.getElementById('nav-' + tabId).classList.add('active');
}

// ================= DEVICE DISCOVERY & CONTROL =================
function refreshPeers() {
    var cmd = { type: "check_peers" };
    if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send(JSON.stringify(cmd));
    }
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
        var dir = p.dir !== undefined ? p.dir : 2;
        var isRunning = p.running === true;
        var isOnline = (p.online !== undefined) ? p.online : true;

        var div = document.createElement('div');
        div.className = 'card peer-card' + (isOnline ? '' : ' offline');
        var safeMac = p.mac.replace(/[^a-zA-Z0-9]/g, '');
        div.id = 'peer-' + safeMac;

        var statusColor = isOnline ? (isRunning ? 'var(--success-color)' : 'var(--text-secondary)') : '#555';

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
                    <label>TPD: <span id="p-tpd-val-${p.mac}">${tpd}</span></label>
                    <input type="range" min="100" max="3000" step="10" value="${tpd}" id="p-tpd-${p.mac}" oninput="document.getElementById('p-tpd-val-${p.mac}').innerText = this.value">
                </div>
                <div class="control-group">
                    <label>${getTrans('duration') || 'Süre'}: <span id="p-dur-val-${p.mac}">${dur}</span></label>
                    <input type="range" min="1" max="120" step="1" value="${dur}" id="p-dur-${p.mac}" oninput="document.getElementById('p-dur-val-${p.mac}').innerText = this.value">
                </div>
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

window.pushPeerSettings = function (mac, currentRunningState) {
    var tpd = parseInt(document.getElementById('p-tpd-' + mac).value);
    var dur = parseInt(document.getElementById('p-dur-' + mac).value);
    // Peer için yön ayarı kaldırıldı veya basitleştirildi varsayıyoruz, 
    // ama eğer varsa ekleyebilirsiniz. Şimdilik varsayılan 2.
    var dir = 2; 

    var cmd = {
        type: "peer_settings",
        target: mac,
        tpd: tpd,
        dur: dur,
        dir: dir,
        running: currentRunningState
    };

    if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send(JSON.stringify(cmd));
        showToast('Ayarlar kutuya gönderildi!');
    }    
};

window.togglePeer = function (mac, newState) {
    var tpd = parseInt(document.getElementById('p-tpd-' + mac).value);
    var dur = parseInt(document.getElementById('p-dur-' + mac).value);
    var dir = 2;

    var cmd = {
        type: "peer_settings",
        target: mac,
        tpd: tpd,
        dur: dur,
        dir: dir,
        running: newState
    };
    if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send(JSON.stringify(cmd));
    }
};

window.deletePeer = function (mac) {
    var msg = (typeof getTrans === 'function' && getTrans('confirm_delete')) ? getTrans('confirm_delete') : "Cihazı silmek istediğinize emin misiniz?";
    if (confirm(msg)) {
        var cmd = { type: "del_peer", target: mac };
        if (socket && socket.readyState === WebSocket.OPEN) {
            socket.send(JSON.stringify(cmd));
        }
    }
};

// ================= WIFI LOGIC (YENİLENMİŞ) =================

// Modal açıldığında çalışacak ana fonksiyon
function openWifiSettings() {
    var setupModal = document.getElementById('setupModal');
    if(setupModal) setupModal.style.display = 'none';

    var wifiModal = document.getElementById('wifiModal');
    if(wifiModal) {
        wifiModal.style.display = 'flex';
        // Modalı açar açmaz taramayı başlat
        scanNetworks();
    }
}

// Backend'deki /scan endpoint'ine istek atar
function scanNetworks() {
    var listDiv = document.getElementById('networkList');
    if(!listDiv) return;

    listDiv.innerHTML = `
        <div class="loader-container">
            <div class="loader"></div>
            <p>${getTrans('scanning') || 'Ağlar taranıyor...'}</p>
        </div>`;

    fetch('/scan')
        .then(response => response.json())
        .then(networks => {
            listDiv.innerHTML = ""; // Yükleniyor yazısını temizle
            
            if (networks.length === 0) {
                listDiv.innerHTML = "<div style='padding:15px; text-align:center;'>Ağ bulunamadı. <br><a href='#' onclick='scanNetworks()'>Tekrar Dene</a></div>";
                return;
            }

            // Sinyal gücüne göre sırala (Güçlüden zayıfa)
            networks.sort((a, b) => b.rssi - a.rssi);

            networks.forEach(net => {
                var item = document.createElement('div');
                item.className = 'network-item';
                
                var lockIcon = net.secure ? '🔒' : '';
                var itemHTML = `
                    <span class="ssid">${net.ssid}</span>
                    <span class="signal">${lockIcon} ${net.rssi} dBm</span>
                `;
                item.innerHTML = itemHTML;
                
                // Tıklanınca inputa doldur
                item.onclick = function() {
                    var ssidInput = document.getElementById('wifiSSID');
                    var passInput = document.getElementById('wifiPass');
                    if(ssidInput) ssidInput.value = net.ssid;
                    if(passInput) passInput.focus();
                    
                    // Görsel seçim efekti
                    document.querySelectorAll('.network-item').forEach(el => el.style.background = 'transparent');
                    item.style.background = 'rgba(253, 203, 110, 0.2)';
                };
                
                listDiv.appendChild(item);
            });
        })
        .catch(err => {
            console.error(err);
            listDiv.innerHTML = "<div style='padding:15px; text-align:center; color:var(--danger-color)'>Tarama Hatası.<br><button class='btn-small' onclick='scanNetworks()'>Tekrar Dene</button></div>";
        });
}

function saveAndConnect() {
    var ssidInput = document.getElementById('wifiSSID');
    var passInput = document.getElementById('wifiPass');
    var nameInput = document.getElementById('setupDeviceName');

    var ssid = ssidInput ? ssidInput.value : "";
    var pass = passInput ? passInput.value : "";
    var name = nameInput ? nameInput.value : "Horus"; 

    if(!ssid) {
        showToast("Lütfen bir Wifi ağı adı girin!", "error");
        return;
    }

    showToast("Ayarlar gönderiliyor...", "info");

    var formData = new FormData();
    formData.append("ssid", ssid);
    formData.append("pass", pass);
    formData.append("name", name);

    fetch('/save-wifi', {
        method: 'POST',
        body: formData
    })
    .then(response => {
        if (response.ok) {
            var wifiModal = document.getElementById('wifiModal');
            if(wifiModal) wifiModal.style.display = 'none';
            
            showToast("Kaydedildi! Cihaz yeniden başlıyor...", "success");
            
            // Arayüzü bilgilendirme moduna al
            document.body.innerHTML = `
                <div style="display:flex; flex-direction:column; align-items:center; justify-content:center; height:100vh; text-align:center; color:#fff; padding:20px;">
                    <h2>Cihaz Yeniden Başlatılıyor</h2>
                    <p>Lütfen telefonunuzu ev ağınıza bağlayın.</p>
                    <p>Birazdan yönlendirileceksiniz:</p>
                    <h3 style="color:var(--accent-color);">http://horus-${deviceSuffix}.local</h3>
                    <div class="loader"></div>
                </div>
            `;

            setTimeout(() => {
                window.location.href = "http://horus-" + deviceSuffix + ".local";
            }, 15000);
            
        } else {
            showToast("Kayıt başarısız oldu!", "error");
        }
    })
    .catch(error => {
        // Wifi kopacağı için fetch hatası normaldir
        console.log(error);
        showToast("Komut gönderildi. Bağlantı kesiliyor...", "info");
    });
}

function skipSetup() {
    var targetAddress = "http://horus-" + deviceSuffix + ".local";
    
    var setupModal = document.getElementById('setupModal');
    if(setupModal) setupModal.style.display = 'none';
    
    showToast("Kontrol paneline yönlendiriliyor: " + targetAddress, "info");
    
    setTimeout(() => {
        window.location.href = targetAddress;
    }, 1500);
}

// ================= OTA LOGIC =================
function triggerAutoUpdate() {
    if (!confirm(getTrans('confirm_update') || "Update firmware?")) return;

    fetch('/api/ota-auto', { method: 'POST' })
        .then(response => response.json())
        .then(data => {
            if (data.status == "started" || data.status == "updating") {
                showToast("Güncelleme başlatıldı! 5 Dakika bekleyip yeniden bağlanın");
                if (!otaStatusInterval) {
                    otaStatusInterval = setInterval(checkOtaStatus, 2000);
                }
            } else if (data.status == "busy") {
                showToast("Güncelleştirmeye devam ediyor.");
            }
        })
        .catch(e => showToast("Güncelleştirme hatası"));
}

function checkOtaStatus() {
    fetch('/api/ota-status')
        .then(response => response.json())
        .then(data => {
            console.log("OTA Status:", data.status);

            if (data.status == "up_to_date") {
                clearInterval(otaStatusInterval);
                otaStatusInterval = null;
                showToast("Yazılım zaten güncel!");
            } else if (data.status == "error") {
                clearInterval(otaStatusInterval);
                otaStatusInterval = null;
                showToast("Güncelleme yapılmadı!");
            } else if (data.status == "idle") {
                clearInterval(otaStatusInterval);
                otaStatusInterval = null;
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
    if(typeof translations === 'undefined') return;
    var t = translations[lang];
    if (!t) t = translations['tr']; 

    document.querySelectorAll('[data-i18n]').forEach(el => {
        var key = el.getAttribute('data-i18n');
        if (t[key]) {
            el.innerText = t[key];
        }
    });

    updateStatusUI();
}

// ================= THEME FUNCTIONS =================
function setTheme(theme) {
    localStorage.setItem('horus_theme', theme);
    document.documentElement.setAttribute('data-theme', theme);

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

    document.querySelectorAll('.color-btn').forEach(btn => {
        btn.classList.remove('active');
        if (btn.getAttribute('data-color') === color) {
            btn.classList.add('active');
        }
    });
}

function showToast(message, type = "info", duration = 2500) {
    const container = document.getElementById("toast-container");
    if (!container) return;

    const toast = document.createElement("div");
    toast.className = `toast ${type}`;
    toast.textContent = message;

    container.appendChild(toast);

    setTimeout(() => {
        toast.style.opacity = "0";
        toast.style.transform = "translateY(10px)";
        setTimeout(() => toast.remove(), 300);
    }, duration);
}
