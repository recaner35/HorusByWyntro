// script.js

gsap.registerPlugin(ScrollTrigger);

// --- 1. HERO ANIMATION ---
const cube = document.querySelector('.cube');
const heroProduct = document.getElementById('hero-product');
if(cube && heroProduct) {
    const rotationTimeline = gsap.to(cube, { rotationX: -15, rotationY: 360, duration: 25, repeat: -1, ease: "none" });

    heroProduct.addEventListener('mouseenter', () => { gsap.to(rotationTimeline, { timeScale: 0.1, duration: 1.5 }); gsap.to(cube, { scale: 1.05, duration: 1 }); });
    heroProduct.addEventListener('mouseleave', () => { gsap.to(rotationTimeline, { timeScale: 1, duration: 1 }); gsap.to(cube, { scale: 1, duration: 1 }); });

    const heroTl = gsap.timeline();
    heroTl.to('#navbar', { opacity: 1, duration: 2 })
          .to('.hero-text', { opacity: 1, filter: "blur(0px)", y: 0, stagger: 0.3, duration: 2, ease: "power2.out" }, "-=1.5");
}

// --- 2. SCROLL STORYTELLING (DUZELTILMIS VE SENKRONIZE EDILMIS) ---
const sections = document.querySelectorAll('section:not(#hero):not(#footer)');

sections.forEach(section => {
  const light = section.querySelector('.gsap-glow');
  const obj = section.querySelector('.gsap-obj');
  const texts = section.querySelectorAll('.gsap-fade-up');
  
  // ÖNEMLİ DÜZELTME: 
  // "preventOverlaps: true" -> Hızlı geçerken animasyonların üst üste binmesini engeller.
  // "fastScrollEnd: true" -> Scroll çok hızlıysa animasyonu beklemeden direkt son haline getirir.
  
  const tl = gsap.timeline({ 
      scrollTrigger: { 
          trigger: section, 
          start: "top 70%", // Ekrana girdikten biraz sonra başla
          end: "bottom 30%", 
          toggleActions: "play reverse play reverse",
          preventOverlaps: true, 
          fastScrollEnd: true 
      } 
  });

  if(light) tl.to(light, { opacity: 1, scale: 1.2, duration: 2, ease: "power2.out" });
  if(obj) tl.to(obj, { opacity: 1, y: 0, duration: 1.5, ease: "power2.out" }, "-=1.5");
  if(texts.length > 0) tl.to(texts, { opacity: 1, y: 0, stagger: 0.15, duration: 1.5, ease: "power2.out" }, "-=1.2");
});

// Footer Animasyonu
const footer = document.querySelector("#footer");
if(footer) {
    gsap.from("#footer p", {
        scrollTrigger: {
            trigger: "#footer",
            start: "top 90%",
            toggleActions: "play reverse play reverse",
            preventOverlaps: true
        },
        opacity: 0,
        y: 20,
        stagger: 0.2,
        duration: 1.5,
        ease: "power2.out"
    });
}

// --- 3. MOBILE MENU ---
const menuBtn = document.getElementById('menu-btn');
const mobileMenu = document.getElementById('mobile-menu');
const mobileLinks = document.querySelectorAll('.mobile-link');
let isMenuOpen = false;

if(menuBtn && mobileMenu) {
    menuBtn.addEventListener('click', () => {
        isMenuOpen = !isMenuOpen;
        if (isMenuOpen) {
            gsap.to(mobileMenu, { opacity: 1, pointerEvents: 'all', duration: 0.5, ease: 'power2.out' });
            gsap.fromTo('.mobile-link', { y: 50, opacity: 0 }, { y: 0, opacity: 1, stagger: 0.1, duration: 0.8, ease: 'power3.out', delay: 0.2 });
            gsap.to(menuBtn.children[0], { rotation: 45, y: 4, width: '24px' });
            gsap.to(menuBtn.children[1], { rotation: -45, y: -4, width: '24px' });
        } else {
            gsap.to(mobileMenu, { opacity: 0, pointerEvents: 'none', duration: 0.5 });
            gsap.to(menuBtn.children[0], { rotation: 0, y: 0, width: '24px' });
            gsap.to(menuBtn.children[1], { rotation: 0, y: 0, width: '16px' });
        }
    });

    mobileLinks.forEach(link => {
        link.addEventListener('click', () => {
            isMenuOpen = false;
            gsap.to(mobileMenu, { opacity: 0, pointerEvents: 'none', duration: 0.5 });
            gsap.to(menuBtn.children[0], { rotation: 0, y: 0 });
            gsap.to(menuBtn.children[1], { rotation: 0, y: 0, width: '16px' });
        });
    });
}

// --- 4. GITHUB API ---
async function fetchLatestRelease() {
    const user = 'recaner35'; 
    const repo = 'HorusByWyntro'; 
    const container = document.getElementById('release-info');
    if(!container) return;

    try {
      const response = await fetch(`https://api.github.com/repos/${user}/${repo}/releases`);
      if (!response.ok) throw new Error("API Hatası");
      const data = await response.json();
      
      if (!Array.isArray(data) || data.length === 0) throw new Error("Henüz release yok");
      
      const latest = data[0]; 
      const date = new Date(latest.published_at).toLocaleDateString('tr-TR', { year: 'numeric', month: 'long', day: 'numeric' });

      container.innerHTML = `
        <div class="opacity-0 translate-y-2 animate-[fadeIn_0.5s_ease-out_forwards]">
            <div class="flex items-center justify-between mb-3">
                <span class="text-white font-bold tracking-wide text-sm">${latest.name || latest.tag_name}</span>
                <a href="${latest.html_url}" target="_blank" class="text-[10px] border border-green-500/40 text-green-400 px-2 py-0.5 rounded hover:bg-green-500/10 transition">${latest.tag_name}</a>
            </div>
            <div class="text-[10px] text-neutral-500 mb-3 font-sans">Yayınlanma: ${date}</div>
            <div class="text-xs text-neutral-300 font-mono whitespace-pre-wrap leading-relaxed border-l-2 border-white/10 pl-3 mb-4 opacity-80">${latest.body ? latest.body : 'Versiyon notu bulunamadı.'}</div>
            <a href="${latest.html_url}" target="_blank" class="inline-flex items-center gap-2 text-[10px] text-white/40 hover:text-white transition group/link">
                <span>GitHub'da İncele / İndir</span><span class="group-hover/link:translate-x-1 transition-transform">→</span>
            </a>
        </div>`;
    } catch (e) { 
      container.innerHTML = `<div class="text-red-400/80 text-[10px] mt-4">[Error]: Bağlantı kurulamadı veya release yok.<br><a href="https://github.com/${user}/${repo}/releases" target="_blank" class="underline">Manuel Kontrol →</a></div>`; 
    }
}
document.addEventListener('DOMContentLoaded', fetchLatestRelease);
