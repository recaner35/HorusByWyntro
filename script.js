/* Horus by Wyntro - Core Logic */

// ===============================
// Dil Tanımlamaları (i18n)
// ===============================
// 'translations' objesi languages.js dosyasından gelir.

let currentLang = 'tr';
let ws;

// ===============================
// Başlangıç
// ===============================
document.addEventListener('DOMContentLoaded', () => {
    loadPreferences();
    connectWebSocket();
    loadPreferences();
    connectWebSocket();
    updateUI();
    setInterval(pollWifiStatus, 2000); // Polling for WiFi status
});

// ===============================
// Dil ve Tema Yönetimi
// ===============================
function changeLanguage(lang) {
    currentLang = lang;
    document.querySelectorAll('[data-i18n]').forEach(el => {
        const key = el.getAttribute('data-i18n');
        if (translations[lang] && translations[lang][key]) {
            el.innerText = translations[lang][key];
        }
    });
    localStorage.setItem('horus_lang', lang);
}

function changeTheme(theme) {
    if (theme === 'auto') {
        const prefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches;
        document.body.setAttribute('data-theme', prefersDark ? 'dark' : 'light');
    } else {
        document.body.setAttribute('data-theme', theme);
    }
    localStorage.setItem('horus_theme', theme);
}

function loadPreferences() {
    const savedLang = localStorage.getItem('horus_lang') || 'tr';
    document.getElementById('langSelect').value = savedLang;
    changeLanguage(savedLang);

    const savedTheme = localStorage.getItem('horus_theme') || 'dark';
    document.getElementById('themeSelect').value = savedTheme;
    changeTheme(savedTheme);

    const savedColor = localStorage.getItem('horus_accent') || '#d4af37';
    setAccentColor(savedColor);
    renderColorPicker(savedColor);
}

// ===============================
// WebSocket Yönetimi
// ===============================
function connectWebSocket() {
    const protocol = window.location.protocol === "https:" ? "wss" : "ws";
    const gateway = `${protocol}://${window.location.hostname}/ws`;

    ws = new WebSocket(gateway);

    ws.onopen = () => {
        console.log('WS Bağlandı');
        document.getElementById('connectionStatus').style.background = '#00b894'; // Yeşil
    };

    ws.onclose = () => {
        console.log('WS Koptu, tekrar deneniyor...');
        document.getElementById('connectionStatus').style.background = '#ff4d4d'; // Kırmızı
        setTimeout(connectWebSocket, 2000);
    };

    ws.onmessage = (event) => {
        const data = JSON.parse(event.data);
        handleServerMessage(data);
    };
}

function handleServerMessage(data) {
    // Sunucudan gelen veriyi işle
    if (data.tpd) {
        document.getElementById('tpdRange').value = data.tpd;
        updateTpdDisplay(data.tpd);
    }
    if (data.dur) {
        document.getElementById('durRange').value = data.dur;
        updateDurDisplay(data.dur);
    }
    if (data.running !== undefined) {
        updateRunState(data.running, data.nextRunInfo);
    }
    if (data.dir !== undefined) {
        setDirectionUI(data.dir);
    }
    if (data.devices) {
        updateDeviceList(data.devices);
    }
}

function sendSettings() {
    const tpd = parseInt(document.getElementById('tpdRange').value);
    const dur = parseInt(document.getElementById('durRange').value);

    const payload = {
        type: 'settings',
        tpd: tpd,
        dur: dur
    };
    if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(payload));
}

// ===============================
// UI İşlemleri
// ===============================
function updateUI() {
    // UI değerlerini varsayılan veya mevcut durumla eşle
    // WS bağlanana kadar varsayılanları koru
    const tpd = document.getElementById('tpdRange').value;
    const dur = document.getElementById('durRange').value;
    updateTpdDisplay(tpd);
    updateDurDisplay(dur);
}

function updateTpdDisplay(val) {
    document.getElementById('tpdValue').innerText = val;
}

function updateDurDisplay(val) {
    document.getElementById('durValue').innerText = val + "s";
}

function toggleSystem() {
    const btn = document.getElementById('mainBtn');
    const isRunning = btn.classList.contains('active');

    // Optimistik UI güncellemesi
    // Asıl durum WS'den gelince teyit edilecek

    const payload = {
        type: 'command',
        action: isRunning ? 'stop' : 'start'
    };
    ws.send(JSON.stringify(payload));
}

function updateRunState(isRunning, infoText) {
    const btn = document.getElementById('mainBtn');
    const txt = document.getElementById('stateText');
    const subTxt = document.getElementById('nextRunText');
    const t = translations[currentLang] || translations['tr']; // Fallback

    if (isRunning) {
        btn.classList.add('active');
        btn.querySelector('span').innerText = t.stop; // "DURDUR"
        txt.innerText = t.running;
        txt.style.color = 'var(--success-color)';

        // Animasyon
        btn.classList.add('pulse-anim');
    } else {
        btn.classList.remove('active');
        btn.querySelector('span').innerText = t.start; // "BAŞLAT"
        txt.innerText = t.stopped;
        txt.style.color = 'var(--danger-color)';
        btn.classList.remove('pulse-anim');
    }

    if (infoText) subTxt.innerText = infoText;
}

function setDirection(dir) {
    // 0: CW, 1: CCW, 2: Bi
    const payload = { type: 'settings', dir: dir };
    ws.send(JSON.stringify(payload));
    setDirectionUI(dir);
}

function setDirectionUI(dir) {
    const opts = document.querySelectorAll('.segment-opt');
    opts.forEach(o => o.classList.remove('selected'));
    if (opts[dir]) opts[dir].classList.add('selected');
}

// ===============================
// Ayarlar Modalı
// ===============================
function openSettingsModal() {
    document.getElementById('settingsModal').classList.add('active');
}
function closeSettingsModal() {
    document.getElementById('settingsModal').classList.remove('active');
}
function showSection(sec) {
    // Single page app basit navigasyon (smooth scroll or hide/show)
    window.scrollTo({ top: 0, behavior: 'smooth' });
}

function saveDeviceName() {
    const name = document.getElementById('deviceNameInput').value;
    ws.send(JSON.stringify({ type: 'sys', name: name }));
    alert("Cihaz adı isteği gönderildi. Yeniden başlatılıyor...");
}

// ===============================
// Cihaz Listesi
// ===============================
function updateDeviceList(devices) {
    const list = document.getElementById('deviceList');
    list.innerHTML = ""; // Temizle

    // Kendi cihazımız (placeholder)
    const myItem = document.createElement('div');
    myItem.className = "device-item";
    myItem.innerHTML = `
        <div class="device-info">
            <span class="device-name">Bu Cihaz (Master)</span>
            <span class="device-ip">Connected</span>
        </div>`;
    list.appendChild(myItem);

    // Diğerleri
    devices.forEach(d => {
        const item = document.createElement('div');
        item.className = "device-item";
        item.innerHTML = `
            <div class="device-info">
                <span class="device-name">${d.name}</span>
                <span class="device-ip">${d.mac}</span>
            </div>
            <button class="icon-btn" onclick="controlRemote('${d.mac}')">⚙️</button>
        `;
        list.appendChild(item);
    });
}


// ===============================
// Renk Paleti Yönetimi
// ===============================
const accentColors = [
    "#d4af37", // Altın (Gold) - Default
    "#ff4d4d", // Kırmızı (Red)
    "#ff9f43", // Turuncu (Orange)
    "#feca57", // Sarı (Yellow)
    "#00b894", // Zümrüt (Emerald)
    "#00cec9", // Turkuaz (Cyan)
    "#0984e3", // Mavi (Blue)
    "#6c5ce7", // Mor (Purple)
    "#fd79a8", // Pembe (Pink)
    "#b2bec3", // Gri (Grey)
    "#636e72", // Koyu Gri (Dark Grey)
    "#2d3436", // Siyahımsı (Blackish)
    "#ffffff"  // Beyaz (White)
];

function renderColorPicker(selectedColor) {
    const container = document.getElementById('colorPickerContainer');
    if (!container) return;
    container.innerHTML = "";

    accentColors.forEach(color => {
        const div = document.createElement('div');
        div.className = 'color-option';
        div.style.backgroundColor = color;
        if (color === selectedColor) {
            div.classList.add('selected');
        }
        div.onclick = () => {
            // Seçimi güncelle
            document.querySelectorAll('.color-option').forEach(el => el.classList.remove('selected'));
            div.classList.add('selected');
            setAccentColor(color);
        };
        container.appendChild(div);
    });
}

function setAccentColor(color) {
    const root = document.documentElement;

    // RGB değerlerine çevirip glow için opacity ekleyeceğiz
    // Basit bir hex to rgba dönüşümü yapalım veya sadece rengi atayıp glow'u update edelim

    root.style.setProperty('--accent-color', color);

    // Hex to RGB conversion for glow
    let r = 0, g = 0, b = 0;
    if (color.length === 4) {
        r = "0x" + color[1] + color[1];
        g = "0x" + color[2] + color[2];
        b = "0x" + color[3] + color[3];
    } else if (color.length === 7) {
        r = "0x" + color[1] + color[2];
        g = "0x" + color[3] + color[4];
        b = "0x" + color[5] + color[6];
    }

    localStorage.setItem('horus_accent', color);
}

// ===============================
// Wi-Fi Yönetimi (Seamless)
// ===============================
let scanPollInterval;

function pollWifiStatus() {
    fetch('/api/wifi-status')
        .then(res => res.json())
        .then(data => {
            // data: {status: "connected"|"connecting"|"disconnected"|"failed", ip: "...", ssid: "..."}
            const statusIndicator = document.getElementById('connectionStatus');
            const statusMsg = document.getElementById('wifiStatusMsg');

            if (data.status === 'connected') {
                statusIndicator.style.background = '#00b894'; // Yeşil
                if (statusMsg) {
                    statusMsg.style.display = 'block';
                    statusMsg.style.color = 'var(--success-color)';
                    statusMsg.innerText = `Bağlandı: ${data.ssid} (${data.ip})`;
                }
            } else if (data.status === 'connecting') {
                statusIndicator.style.background = '#feca57'; // Sarı
                if (statusMsg) {
                    statusMsg.style.display = 'block';
                    statusMsg.style.color = 'var(--accent-color)';
                    statusMsg.innerText = "Bağlanılıyor...";
                }
            } else {
                statusIndicator.style.background = '#ff4d4d'; // Kırmızı
                if (data.status === 'failed' && statusMsg) {
                    statusMsg.style.display = 'block';
                    statusMsg.style.color = 'var(--danger-color)';
                    statusMsg.innerText = "Bağlantı Başarısız!";
                }
            }
        })
        .catch(err => console.log('WiFi poll err:', err));
}

function openWifiModal() {
    document.getElementById('wifiModal').classList.add('active');
}

function closeWifiModal() {
    document.getElementById('wifiModal').classList.remove('active');
    if (scanPollInterval) clearInterval(scanPollInterval);
}

function startWifiScan() {
    document.getElementById('wifiList').innerHTML = "";
    document.getElementById('wifiSpinner').style.display = "block";
    document.getElementById('scanBtn').disabled = true;

    fetch('/api/wifi-scan')
        .then(res => {
            if (res.status === 202) {
                // Tarama başladı, poll et
                if (scanPollInterval) clearInterval(scanPollInterval);
                scanPollInterval = setInterval(pollWifiScanResult, 1000);
            } else {
                // Zaten meşgul
                alert("Tarama zaten sürüyor...");
            }
        })
        .catch(err => {
            console.error(err);
            document.getElementById('wifiSpinner').style.display = "none";
            document.getElementById('scanBtn').disabled = false;
        });
}

function pollWifiScanResult() {
    fetch('/api/wifi-list')
        .then(res => res.json())
        .then(list => {
            if (list.length > 0) {
                // Sonuçlar geldi
                clearInterval(scanPollInterval);
                renderWifiList(list);
                document.getElementById('wifiSpinner').style.display = "none";
                document.getElementById('scanBtn').disabled = false;
            }
            // Boş ise devam et (veya -2 durumu backend'den handle edilmeli, 
            // şimdilik [] dönüyor scan bitene kadar ama backend -2 ise [] dönüyor. 
            // Scan bitince gerçek liste döner. 
            // Eğer hiç ağ yoksa loopa girebilir, backend tarafında timeout koymuştuk.)
        })
        .catch(err => console.log(err));
}

function renderWifiList(list) {
    const container = document.getElementById('wifiList');
    container.innerHTML = "";

    // Güçlüden zayıfa sırala
    list.sort((a, b) => b.rssi - a.rssi);

    list.forEach(net => {
        // RSSI Icon mantığı
        let icon = "📶";
        if (net.rssi > -50) icon = "🟢";
        else if (net.rssi > -70) icon = "🟡";
        else icon = "🔴";

        const div = document.createElement('div');
        div.className = 'wifi-item';
        div.innerHTML = `
            <div class="wifi-info">
                <span class="wifi-ssid">${net.ssid}</span>
                <span class="wifi-meta">${icon} ${net.rssi}dBm ${net.secure ? '🔒' : '🔓'}</span>
            </div>
            <button class="btn-select" onclick="selectWifi('${net.ssid}')">Seç</button>
        `;
        container.appendChild(div);
    });
}

function selectWifi(ssid) {
    document.getElementById('selectedSsid').innerText = ssid;
    document.getElementById('wifiConnectForm').style.display = 'block';
    document.getElementById('wifiPass').value = '';
    document.getElementById('wifiPass').focus();
}

function connectSelectedWifi() {
    const ssid = document.getElementById('selectedSsid').innerText;
    const pass = document.getElementById('wifiPass').value;

    // UI Update
    document.getElementById('connectBtn').disabled = true;
    document.getElementById('connectBtn').innerText = "Connecting...";

    const formData = new FormData();
    formData.append('ssid', ssid);
    formData.append('pass', pass);

    fetch('/api/wifi-connect', {
        method: 'POST',
        body: formData
    })
        .then(res => res.json())
        .then(data => {
            // Backend 'started' dedi
            // UI'da spinner döndür veya mesaj göster
            document.getElementById('wifiStatusMsg').innerText = "Bağlanıyor...";
            document.getElementById('wifiStatusMsg').style.display = "block";
            document.getElementById('connectBtn').disabled = false;
            document.getElementById('connectBtn').innerText = "Bağlan"; // Reset
        })
        .catch(err => {
            alert("Hata: " + err);
            document.getElementById('connectBtn').disabled = false;
        });
}

