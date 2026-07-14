/* ═══════════════════════════════════════════════════════════
   MonEEG Documentation — Interactive Script
   ═══════════════════════════════════════════════════════════ */

document.addEventListener('DOMContentLoaded', () => {
  initEEGCanvas();
  initNavigation();
  initScrollSpy();
  initMobileMenu();
  initCopyButtons();
  initAnimations();
});

/* ── EEG Waveform Canvas (Hero background) ─────────────── */
function initEEGCanvas() {
  const canvas = document.getElementById('hero-canvas');
  if (!canvas) return;

  const ctx = canvas.getContext('2d');
  let animationId;
  let time = 0;

  function resize() {
    canvas.width = canvas.offsetWidth * window.devicePixelRatio;
    canvas.height = canvas.offsetHeight * window.devicePixelRatio;
    ctx.scale(window.devicePixelRatio, window.devicePixelRatio);
  }

  resize();
  window.addEventListener('resize', resize);

  const channels = 8;
  const channelSpacing = canvas.offsetHeight / (channels + 1);

  function draw() {
    const w = canvas.offsetWidth;
    const h = canvas.offsetHeight;

    ctx.clearRect(0, 0, w, h);

    for (let ch = 0; ch < channels; ch++) {
      const y = channelSpacing * (ch + 1);
      const hue = 190 + ch * 8;

      ctx.beginPath();
      ctx.strokeStyle = `hsla(${hue}, 90%, 60%, 0.35)`;
      ctx.lineWidth = 1.2;

      for (let x = 0; x < w; x += 2) {
        const phase = time + ch * 0.7;
        const freq1 = 0.008 + ch * 0.001;
        const freq2 = 0.025 + ch * 0.003;
        const freq3 = 0.06;

        // Simulate alpha + beta + noise
        let val = Math.sin(x * freq1 + phase) * 18;
        val += Math.sin(x * freq2 + phase * 1.3) * 8;
        val += Math.sin(x * freq3 + phase * 2.7) * 3;
        val += (Math.random() - 0.5) * 4;

        // Occasional spike (simulating P300-like artifact)
        if (Math.sin(x * 0.002 + ch + time * 0.3) > 0.97) {
          val += 30 * Math.sin(x * 0.1 + phase);
        }

        if (x === 0) ctx.moveTo(x, y + val);
        else ctx.lineTo(x, y + val);
      }
      ctx.stroke();
    }

    time += 0.02;
    animationId = requestAnimationFrame(draw);
  }

  draw();
}

/* ── Smooth Navigation ─────────────────────────────────── */
function initNavigation() {
  document.querySelectorAll('.nav-link[data-section]').forEach(link => {
    link.addEventListener('click', (e) => {
      e.preventDefault();
      const targetId = link.getAttribute('data-section');
      const target = document.getElementById(targetId);
      if (target) {
        target.scrollIntoView({ behavior: 'smooth' });
        // Close mobile menu if open
        closeMobileMenu();
      }
    });
  });
}

/* ── Scroll Spy (highlight active nav item) ────────────── */
function initScrollSpy() {
  const sections = document.querySelectorAll('.section[id]');
  const navLinks = document.querySelectorAll('.nav-link[data-section]');

  if (!sections.length || !navLinks.length) return;

  const observer = new IntersectionObserver((entries) => {
    entries.forEach(entry => {
      if (entry.isIntersecting) {
        navLinks.forEach(link => link.classList.remove('active'));
        const activeLink = document.querySelector(`.nav-link[data-section="${entry.target.id}"]`);
        if (activeLink) activeLink.classList.add('active');
      }
    });
  }, {
    rootMargin: '-20% 0px -70% 0px',
    threshold: 0
  });

  sections.forEach(section => observer.observe(section));
}

/* ── Mobile Menu ───────────────────────────────────────── */
function initMobileMenu() {
  const toggle = document.getElementById('mobile-toggle');
  const sidebar = document.getElementById('sidebar');
  const backdrop = document.getElementById('sidebar-backdrop');

  if (!toggle || !sidebar) return;

  toggle.addEventListener('click', () => {
    sidebar.classList.toggle('open');
    if (backdrop) backdrop.classList.toggle('visible');
  });

  if (backdrop) {
    backdrop.addEventListener('click', closeMobileMenu);
  }
}

function closeMobileMenu() {
  const sidebar = document.getElementById('sidebar');
  const backdrop = document.getElementById('sidebar-backdrop');
  if (sidebar) sidebar.classList.remove('open');
  if (backdrop) backdrop.classList.remove('visible');
}

/* ── Copy-to-Clipboard ─────────────────────────────────── */
function initCopyButtons() {
  document.querySelectorAll('.code-block-copy').forEach(btn => {
    btn.addEventListener('click', () => {
      const codeBlock = btn.closest('.code-block');
      const pre = codeBlock.querySelector('pre');
      if (!pre) return;

      navigator.clipboard.writeText(pre.textContent).then(() => {
        const original = btn.textContent;
        btn.textContent = 'Copied!';
        btn.style.color = 'var(--accent-tertiary)';
        btn.style.borderColor = 'var(--accent-tertiary)';
        setTimeout(() => {
          btn.textContent = original;
          btn.style.color = '';
          btn.style.borderColor = '';
        }, 2000);
      });
    });
  });
}

/* ── Scroll Animations (fade-in on scroll) ─────────────── */
function initAnimations() {
  const elements = document.querySelectorAll('.animate-on-scroll');

  if (!elements.length) return;

  const observer = new IntersectionObserver((entries) => {
    entries.forEach(entry => {
      if (entry.isIntersecting) {
        entry.target.classList.add('animate-in');
        observer.unobserve(entry.target);
      }
    });
  }, {
    threshold: 0.1,
    rootMargin: '0px 0px -40px 0px'
  });

  elements.forEach(el => observer.observe(el));
}
