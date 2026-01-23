var currentLang = 'tr';
var socket;
var isRunning = false;
var currentDirection = 2; // 0: CW, 1: CCW, 2: Bi-Directional
var wifiScanInterval;
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
    document.getElementById('languageSelect').value = currentLang;
    applyLanguage(currentLang);

    // Tema
    var savedTheme = localStorage.getItem('horus_theme') || 'auto';
    setTheme(savedTheme);

    // Renk
    var savedColor = localStorage.getItem('horus_accent_color') || '#fdcb6e';
    setAccentColor(savedColor);

    // Versiyon
    fetch('/api/version')
        .then(r => r.json())
        .then(d => {
            if (d.version) {
                document.getElementById('fwVersion').innerText = d.version;
            }
        });

    // 🔥 SETUP MODE + WIFI SCAN (ASIL EKSİK PARÇA)
    fetch("/api/device-state")
        .then(r => r.json())
        .then(data => {
            isSetupMode = data.setup;
            if (isSetupMode) {
                document.getElementById("setupCard")?.classList.remove("hidden");
                scanWifi(); // <-- ARTIK GERÇEKTEN ÇALIŞACAK
            }
        });
};

function initWebSocket() {
    var protocol = location.protocol === 'https:' ? 'wss://' : 'ws://';
    socket = new WebSocket(protocol + location.host + '/ws');

    // Otomatik durum güncellemesi (10 saniyede bir)
    if (statusInterval) clearInterval(statusInterval);

    statusInterval = setInterval(function () {
        if (socket && socket.readyState === WebSocket.OPEN && !wifiScanInterval) {
            socket.send(JSON.stringify({ type: "check_peers" }));
        }
    }, 10000);

    socket.onopen = function () {
        console.log('WebSocket Connected');
        document.getElementById('connectionStatus').style.backgroundColor = '#0f0';
    };

    socket.onmessage = function (event) {
        var data;
        try {
            data = JSON.parse(event.data);
        } catch (e) {
            console.error("Bozuk WS verisi:", event.data);
        return;
        }
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
            showToast(data.message);
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

    // Kullanıcıya bilgi ver
    showToast('Cihaz adı kaydedildi. Cihaz yeniden başlatılıyor...');

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
    if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send(JSON.stringify(cmd));
    // Placeholder kaldırıldı - akıcı güncelleme için
    }
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
        var safeMac = p.mac.replace(/[^a-zA-Z0-9]/g, '');
        div.id = 'peer-' + safeMac;

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
                    <label>TPD: <span id="p-tpd-val-${p.mac}">${tpd}</span></label>
                    <input
                        type="range"
                        min="100"
                        max="3000"
                        step="10"
                        value="${tpd}"
                        id="p-tpd-${p.mac}"
                        oninput="document.getElementById('p-tpd-val-${p.mac}').innerText = this.value"
                    >
                </div>

                <div class="control-group">
                    <label>${getTrans('duration') || 'Süre'}:
                        <span id="p-dur-val-${p.mac}">${dur}</span>
                    </label>
                        <input
                        type="range"
                        min="1"
                        max="120"
                        step="1"
                        value="${dur}"
                        id="p-dur-${p.mac}"
                        oninput="document.getElementById('p-dur-val-${p.mac}').innerText = this.value"
                    >
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

    if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send(JSON.stringify(cmd));
    // Görsel geri bildirim
    showToast('Ayarlar kutuya gönderildi!');
    }    
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
    if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send(JSON.stringify(cmd));
        }
};

window.deletePeer = function (mac) {
    var msg = (typeof getTrans === 'function' && getTrans('confirm_delete')) ? getTrans('confirm_delete') : "Cihazı silmek istediğinize emin misiniz?";
    if (confirm(msg)) {
        var cmd = {
            type: "del_peer",
            target: mac
        };
        if (socket && socket.readyState === WebSocket.OPEN) {
            socket.send(JSON.stringify(cmd));
        }
    }
};

// ================= WIFI LOGIC =================
let isScanning = false;

function scanWifi() {
    const list = document.getElementById('wifiList');
    const btn = document.getElementById('scanBtn');

    if (!list || isScanning) return;

    isScanning = true;
    list.innerHTML = '<div class="scanning">Ağlar taranıyor...</div>';
    if (btn) btn.disabled = true;

    // 1️⃣ Tarama tetiklenir
    fetch('/api/scan-networks')
        .then(r => r.json())
        .then(() => {
            // 2️⃣ ESP32 scan süresi
            setTimeout(() => {
                fetch('/api/scan-networks')
                    .then(r => r.json())
                    .then(networks => {
                        renderWifiList(networks);
                        isScanning = false;
                        if (btn) btn.disabled = false;
                    })
                    .catch(() => {
                        list.innerHTML = '<div class="error">Tarama hatası</div>';
                        isScanning = false;
                        if (btn) btn.disabled = false;
                    });
            }, 2500);
        })
        .catch(() => {
            list.innerHTML = '<div class="error">ESP32 bağlantı hatası</div>';
            isScanning = false;
            if (btn) btn.disabled = false;
        });
}

function renderWifiList(networks) {
    const list = document.getElementById('wifiList');
    list.innerHTML = '';
    
    if (networks.length === 0) {
        list.innerHTML = '<div class="empty">Ağ bulunamadı. Tekrar deneyin.</div>';
        return;
    }

    // Tekrarlanan SSID'leri temizle (Opsiyonel ama şık durur)
    const uniqueNetworks = [...new Map(networks.map(item => [item['ssid'], item])).values()];

    uniqueNetworks.forEach(net => {
        const div = document.createElement('div');
        div.className = 'wifi-item';
        // Güvenlik ikonunu ve sinyal seviyesini göster
        const lockIcon = net.secure ? '🔒' : 'OPEN';
        div.innerHTML = `
            <span class="ssid">${net.ssid}</span>
            <span class=\"signal\">${net.rssi} dBm ${lockIcon}</span>
        `;
        div.onclick = () => {
            document.getElementById('ssid').value = net.ssid;
            document.getElementById('pass').focus();
        };
        list.appendChild(div);
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

                // 🔹 SETUP MODUNDAYSA
                if (isSetupMode) {
                    document.getElementById("setupCard")?.classList.add("hidden");
                    document.getElementById("setupDone")?.classList.remove("hidden");

                    setTimeout(() => {
                        // captive portal kapanması için
                        window.location.href = "/";
                    }, 3000);

                } 
                // 🔹 NORMAL MOD (ESKİ DAVRANIŞ)
                else {
                    if (confirm(getTrans('connection_started') + " Reload?")) {
                        location.reload();
                    }
                }

            } else {
                showToast("Bağlantı hatası");
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
            var statusDiv = document.getElementById('statusText'); // Reuse main status for feedback?
            // Or better, just alert user or use a toast. 
            // For now, let's just log and stop if done

            if (data.status == "up_to_date") {
                clearInterval(otaStatusInterval);
                otaStatusInterval = null;
                showToast("Yazılım zaten güncel!");
            } else if (data.status == "error") {
                clearInterval(otaStatusInterval);
                otaStatusInterval = null;
                showToast("Güncelleme yapılmadı!");
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
  fetch("/api/skip-setup", { method: "POST" })
    .then(() => {
      document.getElementById("setupCard").classList.add("hidden");
    });
}

// --- YENİ EKLENEN KODLAR: PWA Kurulum Mantığı ---
let deferredPrompt;
const installBtn = document.getElementById('installBtn');
const iosModal = document.getElementById('iosInstallModal');

// Android: Chrome kurulum olayını yakala
window.addEventListener('beforeinstallprompt', (e) => {
    // Tarayıcının otomatik mini-infobar göstermesini engelle
    e.preventDefault();
    // Olayı daha sonra tetiklemek üzere sakla
    deferredPrompt = e;
    // Butonu görünür yap
    if(installBtn) installBtn.classList.remove('hidden');
});

// iOS Tespiti
const isIos = () => {
    const userAgent = window.navigator.userAgent.toLowerCase();
    return /iphone|ipad|ipod/.test(userAgent);
}

// Sayfa yüklendiğinde iOS kontrolü
window.addEventListener('load', () => {
    // Eğer iOS ise ve uygulama zaten "standalone" (tam ekran/yüklü) modda değilse butonu göster
    const isStandalone = ('standalone' in window.navigator) && (window.navigator.standalone);
    if (isIos() && !isStandalone) {
        if(installBtn) installBtn.classList.remove('hidden');
    }
});

// Butona tıklandığında çalışacak fonksiyon
function handleInstallClick() {
    if (isIos()) {
        // iOS ise talimat pencresini aç
        if(iosModal) iosModal.classList.remove('hidden');
    } else {
        // Android/Chrome ise native kurulumu tetikle
        if (deferredPrompt) {
            deferredPrompt.prompt();
            deferredPrompt.userChoice.then((choiceResult) => {
                if (choiceResult.outcome === 'accepted') {
                    console.log('Kullanıcı kurulumu kabul etti');
                } else {
                    console.log('Kullanıcı kurulumu reddetti');
                }
                deferredPrompt = null;
            });
        }
    }
}

// iOS modalını kapatma
function closeIosModal() {
    if(iosModal) iosModal.classList.add('hidden');
}










