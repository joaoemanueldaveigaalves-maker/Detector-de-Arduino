/* =========================================================
   DETECTOR DE MENTIRAS COM ARDUINO — script.js
   ========================================================= */

document.addEventListener('DOMContentLoaded', () => {

  /* ---------- MENU FIXO + ESTADO AO ROLAR ---------- */
  const nav = document.getElementById('nav');
  const toTopBtn = document.getElementById('toTop');
  window.addEventListener('scroll', () => {
    nav.classList.toggle('scrolled', window.scrollY > 20);
    toTopBtn.classList.toggle('visible', window.scrollY > 500);
  });

  /* ---------- MENU RESPONSIVO (CELULAR) ---------- */
  const navToggle = document.getElementById('navToggle');
  const navLinks = document.getElementById('navLinks');
  navToggle.addEventListener('click', () => {
    const isOpen = navLinks.classList.toggle('open');
    navToggle.classList.toggle('open', isOpen);
    navToggle.setAttribute('aria-expanded', isOpen);
  });
  navLinks.querySelectorAll('a').forEach(link => {
    link.addEventListener('click', () => {
      navLinks.classList.remove('open');
      navToggle.classList.remove('open');
      navToggle.setAttribute('aria-expanded', 'false');
    });
  });

  /* ---------- BOTÃO VOLTAR AO TOPO ---------- */
  toTopBtn.addEventListener('click', () => {
    window.scrollTo({ top: 0, behavior: 'smooth' });
  });

  /* ---------- ANIMAÇÕES AO ENTRAR NA TELA (scroll reveal) ---------- */
  const revealEls = document.querySelectorAll('.reveal');
  const revealObserver = new IntersectionObserver((entries) => {
    entries.forEach(entry => {
      if (entry.isIntersecting) {
        entry.target.classList.add('in-view');
        revealObserver.unobserve(entry.target);
      }
    });
  }, { threshold: 0.15 });
  revealEls.forEach(el => revealObserver.observe(el));

  /* ---------- EFEITO DE DIGITAÇÃO NO TÍTULO ---------- */
  const typedEl = document.getElementById('typedTitle');
  const fullText = 'Detector de Mentiras com Arduino';
  const prefersReducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches;

  if (typedEl && !prefersReducedMotion) {
    typedEl.textContent = '';
    const cursor = document.createElement('span');
    cursor.className = 'cursor';
    cursor.innerHTML = '&nbsp;';
    let i = 0;
    function typeChar() {
      if (i <= fullText.length) {
        typedEl.textContent = fullText.slice(0, i);
        typedEl.appendChild(cursor);
        i++;
        setTimeout(typeChar, 45);
      } else {
        setTimeout(() => cursor.remove(), 1400);
      }
    }
    typeChar();
  }

  /* ---------- BOTÃO "MOSTRAR CÓDIGO COMPLETO" ---------- */
  const codeToggle = document.getElementById('codeToggle');
  const codeBlock = document.getElementById('codeBlock');
  codeToggle.addEventListener('click', () => {
    const expanded = codeBlock.classList.toggle('expanded');
    codeToggle.textContent = expanded ? 'Ocultar código' : 'Mostrar código completo';
    codeToggle.setAttribute('aria-expanded', expanded);
  });

  /* ---------- TOTAL DO CUSTO (soma automática da tabela) ---------- */
  const costRows = document.querySelectorAll('#costTable tbody tr[data-valor]');
  if (costRows.length) {
    const total = [...costRows].reduce((sum, r) => sum + Number(r.dataset.valor), 0);
    const t1 = document.getElementById('costTotal');
    const t2 = document.getElementById('costTableTotal');
    if (t1) t1.innerHTML = 'R$ ' + total + '<span class="cost-total-unit">,00</span>';
    if (t2) t2.textContent = 'R$ ' + total + ',00';
  }

  function wrapText(ctx, text, x, y, maxWidth, lineHeight) {
    const words = text.split(' ');
    let line = '';
    let lines = [];
    for (const word of words) {
      const test = line + word + ' ';
      if (ctx.measureText(test).width > maxWidth && line !== '') {
        lines.push(line.trim());
        line = word + ' ';
      } else {
        line = test;
      }
    }
    lines.push(line.trim());
    ctx.textAlign = 'center';
    lines.forEach((l, idx) => ctx.fillText(l, x, y + idx * lineHeight));
  }

  function debounce(fn, wait) {
    let t;
    return (...args) => {
      clearTimeout(t);
      t = setTimeout(() => fn(...args), wait);
    };
  }

});