const CACHE_NAME = 'horus-v1.0.2';
const ASSETS = [
  '/',
  '/index.html',
  '/style.css',
  '/script.js',
  '/languages.js',
  '/manifest.json',
  '/192x192.png',
  '/512x512.png'
];

// Kurulum: Dosyaları önbelleğe al
self.addEventListener('install', (event) => {
  event.waitUntil(
    caches.open(CACHE_NAME).then((cache) => {
      return cache.addAll(ASSETS);
    })
  );
});

// Strateji: Önce Ağ, sonra Önbellek (Güncel kod için)
self.addEventListener('fetch', (event) => {
  event.respondWith(
    fetch(event.request).catch(() => {
      return caches.match(event.request);
    })
  );
});
