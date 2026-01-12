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
// --- 3. GITHUB RELEASES (SADE & ŞIK VERSİYON) ---
async function fetchGitHubReleases() {
    const container = document.getElementById('release-info');
    if(!container) return;

    try {
        // GitHub API'den veriyi çek
        const response = await fetch('https://api.github.com/repos/recaner35/HorusByWyntro/releases');
        
        if (!response.ok) throw new Error('Veri alınamadı');
        
        const data = await response.json();
        
        if (data.length > 0) {
            const latest = data[0];
            
            // 1. Versiyon
            const version = latest.tag_name;

            // 2. Not (Boşsa varsayılan metin)
            const note = (latest.body && latest.body.trim() !== "") 
                         ? latest.body 
                         : "İyileştirmeler yapıldı.";

            // 3. ŞIK HTML ÇIKTISI
            container.innerHTML = `
            <div class="bg-white/5 border border-white/10 backdrop-blur-md rounded-sm p-8 text-left transition hover:bg-white/10 duration-500">
                <div class="flex flex-col md:flex-row md:items-center justify-between gap-4 mb-6 border-b border-white/5 pb-4">
                    <div>
                        <div class="text-[10px] text-neutral-500 tracking-[0.2em] uppercase mb-1">GÜNCEL FIRMWARE</div>
                        <div class="text-3xl text-white font-thin tracking-wider">${version}</div>
                    </div>
                    <div class="text-[10px] px-3 py-1 border border-green-500/30 text-green-400/80 rounded-full uppercase tracking-widest self-start md:self-center">
                        Active
                    </div>
                </div>
                
                <div>
                    <div class="text-[10px] text-neutral-500 tracking-[0.2em] uppercase mb-2">SÜRÜM NOTU</div>
                    <div class="text-sm text-neutral-300 font-light leading-relaxed whitespace-pre-line">
                        ${note}
                    </div>
                </div>
            </div>`;
        } else {
            // Release yoksa
            container.innerHTML = '<div class="text-neutral-500 text-sm tracking-widest italic">Henüz yayınlanmış bir sürüm bulunmuyor.</div>';
        }
    } catch (e) {
        console.error("GitHub hatası:", e);
        // Hata durumunda (Offline veya API limiti)
        container.innerHTML = `
        <div class="bg-white/5 border border-white/10 backdrop-blur-md rounded-sm p-8 text-left">
             <div class="mb-4">
                <div class="text-[10px] text-neutral-500 tracking-[0.2em] uppercase mb-1">FIRMWARE</div>
                <div class="text-xl text-white font-thin">v1.0.0</div>
            </div>
            <div>
                <div class="text-[10px] text-neutral-500 tracking-[0.2em] uppercase mb-2">SÜRÜM NOTU</div>
                <div class="text-sm text-neutral-400 font-light">
                    Sistem kararlı. İyileştirmeler yapıldı.
                </div>
            </div>
        </div>`;
    }
}

// Fonksiyonu Başlat
fetchGitHubReleases();
