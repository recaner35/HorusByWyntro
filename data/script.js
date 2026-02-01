var currentLang = 'tr';
var socket;
var isRunning = false;
var currentDirection = 2; // 0: CW, 1: CCW, 2: Bi-Directional
var wifiScanInterval;
var otaStatusInterval;
var statusInterval;
var deviceSuffix = "";

// --- CONFIG & URLS ---
const API_VERSION_URL = '/api/version';
const API_DEVICE_STATE_URL = '/api/device-state';
const API_SCAN_NETWORKS_URL = '/api/scan-networks';
const API_SAVE_WIFI_URL = '/api/save-wifi';
const API_SKIP_SETUP_URL = '/api/skip-setup';
const API_OTA_AUTO_URL = '/api/ota-auto';
const API_OTA_STATUS_URL = '/api/ota-status';
const API_REBOOT_URL = '/api/reboot';
// ---------------------

// Dil dosyasından çeviri al
function getTrans(key) {
    if (typeof translations !== 'undefined' && translations[currentLang] && translations[currentLang][key]) {
        return translations[currentLang][key];
    }
    return key;
}

let isSetupMode = false;

window.onload = function () {
    initWebSocket();

    // Dil ayarları
    var userLang = navigator.language || navigator.userLanguage;
    var savedLang = localStorage.getItem('horus_lang');

    if (savedLang) {
        currentLang = savedLang;
    } else {
        var langCode = userLang.split('-')[0];
        currentLang = translations[langCode] ? langCode : 'tr';
    }
    var langSelect = document.getElementById('languageSelect');
    if (langSelect) langSelect.value = currentLang;
    applyLanguage(currentLang);

    // Tema
    var savedTheme = localStorage.getItem('horus_theme') || 'auto';
    setTheme(savedTheme);

    // Renk
    var savedColor = localStorage.getItem('horus_accent_color') || '#fdcb6e';
    setAccentColor(savedColor);

    // Versiyon
    fetch(API_VERSION_URL)
        .then(r => r.json())
        .then(d => {
            if (d.version) {
                var vEl = document.getElementById('fwVersion');
                if (vEl) vEl.innerText = d.version;
            }
        });

    // 🔥 SETUP MODE + UI KİLİTLEME DÜZELTMESİ
    fetch(API_DEVICE_STATE_URL)
        .then(r => r.json())
        .then(data => {
            isSetupMode = data.setup;
            if (data.suffix) deviceSuffix = data.suffix;
            if (isSetupMode) {
                // 1. Setup Modu Aktifse Body'ye sınıf ekle (CSS ile yönetmek için)
                document.body.classList.add('setup-mode-active');

                // 2. Navigasyon menüsünü JS ile zorla gizle
                var navBar = document.querySelector('nav');
                if (navBar) navBar.style.display = 'none';

                // 3. Sadece Setup kartını göster
                var setupCard = document.getElementById("setupCard");
                if (setupCard) {
                    setupCard.classList.remove("hidden");
                    // Diğer her şeyi gizle
                    var otherTabs = document.querySelectorAll('.tab-content');
                    otherTabs.forEach(t => {
                        if (t.id !== 'setupCard') t.style.display = 'none';
                    });
                }

                // 4. Taramayı başlat
                scanWifi();
            }
        });
};

function getSignalSvg(rssi) {
    // RSSI Değerleri: -50 (Mükemmel), -60 (İyi), -70 (Orta), Daha düşük (Zayıf)
    if (rssi >= -55) {
        // 4 Diş (Mükemmel)
        return `<svg class="signal-icon" viewBox="0 0 24 24"><path d="M12 3C7.79 3 3.7 4.41 0.38 7H0L12 21L24 7H23.62C20.3 4.41 16.21 3 12 3Z" /></svg>`;
    } else if (rssi >= -65) {
        // 3 Diş (İyi)
        return `<svg class="signal-icon" viewBox="0 0 24 24"><path d="M12 3C7.79 3 3.7 4.41 0.38 7H0L12 21L24 7H23.62C20.3 4.41 16.21 3 12 3Z" fill-opacity="0.3"/><path d="M12 9C9.3 9 6.68 9.89 4.63 11.5L12 21L19.37 11.5C17.32 9.89 14.7 9 12 9Z" /></svg>`;
    } else if (rssi >= -75) {
        // 2 Diş (Orta)
        return `<svg class="signal-icon" viewBox="0 0 24 24"><path d="M12 3C7.79 3 3.7 4.41 0.38 7H0L12 21L24 7H23.62C20.3 4.41 16.21 3 12 3Z" fill-opacity="0.3"/><path d="M12 14C10.65 14 9.4 14.45 8.35 15.2L12 21L15.65 15.2C14.6 14.45 13.35 14 12 14Z" /></svg>`;
    } else {
        // 1 Diş (Zayıf)
        return `<svg class="signal-icon" viewBox="0 0 24 24"><path d="M12 3C7.79 3 3.7 4.41 0.38 7H0L12 21L24 7H23.62C20.3 4.41 16.21 3 12 3Z" fill-opacity="0.3"/><path d="M12 18C11.5 18 11 18.2 10.6 18.5L12 21L13.4 18.5C13 18.2 12.5 18 12 18Z" /></svg>`;
    }
}

function initWebSocket() {
    var protocol = location.protocol === 'https:' ? 'wss://' : 'ws://';
    socket = new WebSocket(protocol + location.host + '/ws');

    if (statusInterval) clearInterval(statusInterval);

    statusInterval = setInterval(function () {
        if (socket && socket.readyState === WebSocket.OPEN && !wifiScanInterval) {
            socket.send(JSON.stringify({ type: "check_peers" }));
        }
    }, 10000);

    socket.onopen = function () {
        console.log('WebSocket Connected');
        var statusEl = document.getElementById('connectionStatus');
        if (statusEl) statusEl.style.backgroundColor = '#0f0';
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
            if (document.getElementById('tpdPayload')) document.getElementById('tpdPayload').value = data.tpd;
            if (document.getElementById('tpdValue')) document.getElementById('tpdValue').innerText = data.tpd;
        }
        if (data.dur !== undefined) {
            if (document.getElementById('durPayload')) document.getElementById('durPayload').value = data.dur;
            if (document.getElementById('durValue')) document.getElementById('durValue').innerText = data.dur;
        }
        if (data.dir !== undefined) {
            setDirectionUI(data.dir);
        }
        if (data.name !== undefined) {
            if (document.getElementById('deviceName')) document.getElementById('deviceName').value = data.name;
        }
        if (data.suffix !== undefined) {
            deviceSuffix = data.suffix;
        }

        if (data.espnow !== undefined) {
            if (document.getElementById('espNowToggle')) document.getElementById('espNowToggle').checked = data.espnow;
        }

        if (data.peers) {
            renderPeers(data.peers);
        }

        if (data.type === "error") {
            showToast(data.message);
        }
    };

    socket.onclose = function () {
        console.log('WebSocket Disconnected');
        var statusEl = document.getElementById('connectionStatus');
        if (statusEl) statusEl.style.backgroundColor = '#f00';
        setTimeout(initWebSocket, 2000);
    };
}

function updateStatusUI() {
    var statusText = document.getElementById('statusText');
    var toggleBtn = document.getElementById('toggleBtn');

    if (!statusText || !toggleBtn) return;

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
    var cw = document.getElementById('dirCW');
    var ccw = document.getElementById('dirCCW');
    var bi = document.getElementById('dirBi');

    if (cw) cw.className = "dir-btn" + (dir == 0 ? " active" : "");
    if (ccw) ccw.className = "dir-btn" + (dir == 1 ? " active" : "");
    if (bi) bi.className = "dir-btn" + (dir == 2 ? " active" : "");
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
    showToast(getTrans('kaydedildi'));
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
    // Setup modundaysak sekme değişimini engelle
    if (isSetupMode) return;

    var contents = document.getElementsByClassName('tab-content');
    for (var i = 0; i < contents.length; i++) {
        contents[i].classList.remove('active');
    }

    var navs = document.getElementsByClassName('nav-btn');
    for (var i = 0; i < navs.length; i++) {
        navs[i].classList.remove('active');
    }

    document.getElementById(tabId).classList.add('active');
    var navBtn = document.getElementById('nav-' + tabId);
    if (navBtn) navBtn.classList.add('active');
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
    if (!list) return;

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
    var dir = 2; // Default varsayılan

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

// ================= WIFI LOGIC (TEK VE ORTAK FONKSİYON) =================
let isScanning = false;

function scanWifi() {
    // Hem Setup modundaki hem de Ana ekrandaki listeyi ve butonları seç
    // Setup Modu ID'leri: wifiList, scanBtn
    // Ana Ekran ID'leri: wifi-list, btn-scan (HTML'deki ID'lerine göre burayı kontrol et)

    // Hangi liste görünürse onu kullan
    let list = document.getElementById('wifiList');
    if (!list || list.offsetParent === null) {
        list = document.getElementById('wifiList'); // Ana ekrandaki listenin ID'si
    }

    // Hangi buton görünürse onu kullan
    let btn = document.getElementById('scanBtn');
    if (!btn || btn.offsetParent === null) {
        btn = document.getElementById('btn-scan'); // Ana ekrandaki butonun ID'si
    }

    if (!list) {
        console.error("Wifi listesi elemanı bulunamadı!");
        return;
    }

    if (isScanning) return;

    isScanning = true;
    list.innerHTML = '<div class="scanning">Ağlar taranıyor...</div>';

    if (btn) {
        btn.disabled = true;
        btn.innerHTML = (typeof getTrans === 'function' && getTrans('scanning')) ? getTrans('scanning') : "Taranıyor...";
    }

    // 1️⃣ Tarama isteği gönder
    fetch(API_SCAN_NETWORKS_URL)
        .then(r => r.json())
        .then(() => {
            // 2️⃣ 2.5 sn bekle, sonra sonucu iste
            setTimeout(() => {
                fetchResults(list, btn);
            }, 2500);
        })
        .catch(() => {
            list.innerHTML = '<div class="error">ESP32 bağlantı hatası</div>';
            resetScanState(btn);
        });
}

// Sonuçları getiren yardımcı fonksiyon
function fetchResults(list, btn) {
    fetch(API_SCAN_NETWORKS_URL)
        .then(r => r.json())
        .then(networks => {
            // Liste boşsa tekrar dene (Retry)
            if (networks.length === 0) {
                list.innerHTML = '<div class="scanning">Ağlar aranıyor... (Tekrar deneniyor)</div>';
                setTimeout(() => {
                    fetchResults(list, btn);
                }, 2000);
                return;
            }

            renderWifiList(networks, list); // Listeyi parametre olarak gönder
            resetScanState(btn);
        })
        .catch(() => {
            list.innerHTML = '<div class="error">Tarama hatası</div>';
            resetScanState(btn);
        });
}

function resetScanState(btn) {
    isScanning = false;
    if (btn) {
        btn.disabled = false;
        // Buton metnini eski haline getir (İkonlu haline veya sadece yazıya)
        btn.innerHTML = (typeof getTrans === 'function' && getTrans('scan')) ? getTrans('scan') : "Ağları Tara";
    }
}

// Listeyi ekrana basan fonksiyon (Güncellendi)
function renderWifiList(networks, listElement) {
    // Eğer parametre olarak liste gelmediyse bulmaya çalış
    const list = listElement || document.getElementById('wifiList');

    list.innerHTML = '';

    if (networks.length === 0) {
        list.innerHTML = '<div class="empty">Ağ bulunamadı. Tekrar deneyin.</div>';
        return;
    }

    const uniqueNetworks = [...new Map(networks.map(item => [item['ssid'], item])).values()];

    uniqueNetworks.forEach(net => {
        const div = document.createElement('div');
        div.className = 'wifi-item';

        const lockIcon = net.secure ? '🔒' : '';
        const signalDisplay = (typeof getSignalSvg === 'function') ? getSignalSvg(net.rssi) : net.rssi + " dBm";

        div.innerHTML = `
            <span class="ssid">${net.ssid}</span>
            <div class="wifi-meta">
                ${lockIcon}
                ${signalDisplay}
            </div>
        `;

        div.onclick = () => {
            openWifiModal(net.ssid);
        };

        list.appendChild(div);
    });
}

function openWifiModal(ssid) {
    var modal = document.getElementById('wifiModal');
    if (modal) {
        document.getElementById('modalSSID').innerText = ssid;
        modal.style.display = 'block';
        setTimeout(() => document.getElementById('wifiPass').focus(), 100);
    }
}

function closeWifiModal() {
    var modal = document.getElementById('wifiModal');
    if (modal) modal.style.display = 'none';
    document.getElementById('wifiPass').value = '';
}

window.onclick = function (event) {
    var modal = document.getElementById('wifiModal');
    if (event.target == modal) {
        closeWifiModal();
    }
}

function connectWifi() {
    var ssid = document.getElementById('modalSSID').innerText;
    var pass = document.getElementById('wifiPass').value;

    showToast(getTrans('connection_started') || "Bağlantı isteği gönderiliyor...", "info");

    fetch(API_SAVE_WIFI_URL, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ssid: ssid, pass: pass })
    })
        .then(response => {
            if (response.ok) {
                closeWifiModal();
                showToast(getTrans('kaydedildi') || "Bilgiler kaydedildi, Horus bağlanıyor...", "success");

                if (isSetupMode) {
                    startRedirectSequence();
                } else {
                    setTimeout(() => {
                        location.reload();
                    }, 3000);
                }
            } else {
                showToast("Bağlantı hatası", "error");
            }
        })
        .catch((err) => {
            console.error("Fetch error during wifi save:", err);
            // Setup modundaysak, fetch hatası genellikle cihazın restart olması demektir.
            // Bu yüzden hataya rağmen yönlendirme sekansını başlatıyoruz.
            if (isSetupMode) {
                closeWifiModal();
                startRedirectSequence();
            } else {
                showToast("Bağlantı isteği başarısız", "error");
            }
        });
}

let redirectTargetUrl = "";

function startRedirectSequence() {
    const setupCard = document.getElementById("setupCard");
    const setupDone = document.getElementById("setupDone");
    const counterEl = document.getElementById("redirectCounter");
    const urlTextEl = document.getElementById("mdnsUrlText");

    if (setupCard) setupCard.classList.add("hidden");
    if (setupDone) setupDone.classList.remove("hidden");

    // mDNS URL'ini hesapla
    let hostnameValue = "Horus";
    const nameInput = document.getElementById('deviceName');
    if (nameInput && nameInput.value.trim() !== "") {
        hostnameValue = nameInput.value;
    }

    let slug = hostnameValue.toLowerCase()
        .replace(/ş/g, 's').replace(/Ş/g, 's')
        .replace(/ı/g, 'i').replace(/İ/g, 'i')
        .replace(/ğ/g, 'g').replace(/Ğ/g, 'g')
        .replace(/ü/g, 'u').replace(/Ü/g, 'u')
        .replace(/ö/g, 'o').replace(/Ö/g, 'o')
        .replace(/ç/g, 'c').replace(/Ç/g, 'c')
        .replace(/[^a-z0-9]/g, '-')
        .replace(/--+/g, '-')
        .replace(/^-|-$/g, '');

    if (!slug) slug = "horus";

    // Eğer setup modundaysak suffix her zaman eklenir
    redirectTargetUrl = "http://" + slug + "-" + deviceSuffix + ".local";
    if (urlTextEl) urlTextEl.innerText = redirectTargetUrl.replace("http://", "");

    let count = 10;
    const timer = setInterval(() => {
        count--;
        if (counterEl) counterEl.innerText = count;

        if (count <= 0) {
            clearInterval(timer);
            window.location.href = redirectTargetUrl;
        }
    }, 1000);
}

function manualRedirect() {
    if (redirectTargetUrl) {
        window.location.href = redirectTargetUrl;
    }
}

// ================= OTA LOGIC =================
function triggerAutoUpdate() {
    if (!confirm(getTrans('confirm_update') || "Update firmware?")) return;

    fetch(API_OTA_AUTO_URL, { method: 'POST' })
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
    fetch(API_OTA_STATUS_URL)
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

function skipSetup() {
    fetch(API_SKIP_SETUP_URL, { method: "POST" })
        .then(() => {
            document.getElementById("setupCard").classList.add("hidden");
            // Menüyü geri getir
            var navBar = document.querySelector('nav');
            if (navBar) navBar.style.display = 'flex';
        });
}

// --- PWA Kurulum Mantığı ---
let deferredPrompt;
const installBtn = document.getElementById('installBtn');
const iosModal = document.getElementById('iosInstallModal');

window.addEventListener('beforeinstallprompt', (e) => {
    e.preventDefault();
    deferredPrompt = e;
    if (installBtn) installBtn.classList.remove('hidden');
});

const isIos = () => {
    const userAgent = window.navigator.userAgent.toLowerCase();
    return /iphone|ipad|ipod/.test(userAgent);
}

window.addEventListener('load', () => {
    const isStandalone = ('standalone' in window.navigator) && (window.navigator.standalone);
    if (isIos() && !isStandalone) {
        if (installBtn) installBtn.classList.remove('hidden');
    }
});

function handleInstallClick() {
    if (isIos()) {
        if (iosModal) iosModal.classList.remove('hidden');
    } else {
        if (deferredPrompt) {
            deferredPrompt.prompt();
            deferredPrompt.userChoice.then((choiceResult) => {
                deferredPrompt = null;
            });
        }
    }
}

function closeIosModal() {
    if (iosModal) iosModal.classList.add('hidden');
}

function rebootDevice() {
    if (confirm(getTrans('confirm_reboot') || "Cihazı yeniden başlatmak istediğinize emin misiniz?")) {
        showToast("Cihaz yeniden başlatılıyor...", "info");
        fetch(API_REBOOT_URL, { method: 'POST' })
            .then(() => {
                setTimeout(() => {
                    location.reload();
                }, 5000);
            })
            .catch(() => showToast("Yeniden başlatma başarısız", "error"));
    }
}


