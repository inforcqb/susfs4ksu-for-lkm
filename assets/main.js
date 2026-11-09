(() => {
  const header = document.querySelector('.site-header');
  const nav = document.getElementById('site-nav');
  const navToggle = document.querySelector('.nav-toggle');
  const reduceMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches;

  const setMenu = (open) => {
    if (!nav || !navToggle) return;
    nav.classList.toggle('is-open', open);
    navToggle.setAttribute('aria-expanded', String(open));
    navToggle.setAttribute('aria-label', open ? '关闭导航' : '打开导航');
  };

  navToggle?.addEventListener('click', () => {
    setMenu(navToggle.getAttribute('aria-expanded') !== 'true');
  });

  nav?.addEventListener('click', (event) => {
    if (event.target.closest('a')) setMenu(false);
  });

  document.addEventListener('keydown', (event) => {
    if (event.key !== 'Escape' || navToggle?.getAttribute('aria-expanded') !== 'true') return;
    setMenu(false);
    navToggle.focus();
  });

  document.addEventListener('click', (event) => {
    if (navToggle?.getAttribute('aria-expanded') !== 'true') return;
    if (nav?.contains(event.target) || navToggle?.contains(event.target)) return;
    setMenu(false);
  });

  window.addEventListener('resize', () => {
    if (window.innerWidth > 900) setMenu(false);
  });

  const onScroll = () => {
    header?.classList.toggle('is-scrolled', window.scrollY > 12);
  };
  onScroll();
  window.addEventListener('scroll', onScroll, { passive: true });

  const navLinks = [...document.querySelectorAll('.site-nav a[href^="#"]')];
  const sections = navLinks
    .map((link) => document.querySelector(link.getAttribute('href')))
    .filter(Boolean);

  const setActiveLink = (id) => {
    navLinks.forEach((link) => {
      link.classList.toggle('is-active', link.getAttribute('href') === `#${id}`);
    });
  };

  let navTicking = false;
  const updateActiveLink = () => {
    navTicking = false;
    const probe = window.scrollY + (header?.offsetHeight || 74) + 90;
    let current = '';
    sections.forEach((section) => {
      if (section.getBoundingClientRect().top + window.scrollY <= probe) current = section.id;
    });
    setActiveLink(current);
  };

  const requestActiveUpdate = () => {
    if (navTicking) return;
    navTicking = true;
    window.requestAnimationFrame(updateActiveLink);
  };

  if (sections.length) {
    updateActiveLink();
    window.addEventListener('scroll', requestActiveUpdate, { passive: true });
    window.addEventListener('resize', requestActiveUpdate);
    navLinks.forEach((link) => {
      link.addEventListener('click', () => setActiveLink(link.getAttribute('href').slice(1)));
    });
  }

  document.querySelectorAll('.tabs').forEach((tabList) => {
    const tabs = [...tabList.querySelectorAll('[role="tab"]')];

    const activate = (tab, focus = false) => {
      tabs.forEach((item) => {
        const selected = item === tab;
        item.setAttribute('aria-selected', String(selected));
        item.tabIndex = selected ? 0 : -1;
        const panel = document.getElementById(item.getAttribute('aria-controls'));
        if (panel) panel.hidden = !selected;
      });
      if (focus) tab.focus();
    };

    tabs.forEach((tab) => {
      tab.addEventListener('click', () => activate(tab));
    });

    tabList.addEventListener('keydown', (event) => {
      const current = tabs.indexOf(document.activeElement);
      if (current < 0) return;

      let next = current;
      if (event.key === 'ArrowRight' || event.key === 'ArrowDown') next = (current + 1) % tabs.length;
      else if (event.key === 'ArrowLeft' || event.key === 'ArrowUp') next = (current - 1 + tabs.length) % tabs.length;
      else if (event.key === 'Home') next = 0;
      else if (event.key === 'End') next = tabs.length - 1;
      else return;

      event.preventDefault();
      activate(tabs[next], true);
    });
  });

  const copyText = async (text) => {
    if (navigator.clipboard && window.isSecureContext) {
      try {
        await navigator.clipboard.writeText(text);
        return;
      } catch {
        // Fall back to the selection-based path below.
      }
    }

    const textarea = document.createElement('textarea');
    textarea.value = text;
    textarea.setAttribute('readonly', '');
    textarea.style.position = 'fixed';
    textarea.style.top = '-9999px';
    textarea.style.opacity = '0';
    document.body.appendChild(textarea);
    textarea.focus();
    textarea.select();
    textarea.setSelectionRange(0, textarea.value.length);
    const copied = document.execCommand('copy');
    textarea.remove();
    if (!copied) throw new Error('copy failed');
  };

  document.querySelectorAll('[data-copy]').forEach((button) => {
    button.addEventListener('click', async () => {
      const pane = button.closest('.code-pane');
      const code = pane?.querySelector('pre code');
      if (!code) return;

      const original = button.textContent;
      try {
        await copyText(code.textContent.replace(/\r\n/g, '\n').trim());
        button.textContent = '已复制';
        button.classList.add('is-copied');
      } catch {
        button.textContent = '复制失败';
      }

      window.setTimeout(() => {
        button.textContent = original;
        button.classList.remove('is-copied');
      }, 1800);
    });
  });

  const revealItems = [...document.querySelectorAll('.reveal')];
  if (reduceMotion || !('IntersectionObserver' in window)) {
    revealItems.forEach((item) => item.classList.add('is-visible'));
  } else {
    const revealObserver = new IntersectionObserver((entries, observer) => {
      entries.forEach((entry) => {
        if (!entry.isIntersecting) return;
        entry.target.classList.add('is-visible');
        observer.unobserve(entry.target);
      });
    }, { rootMargin: '0px 0px -7% 0px', threshold: 0.05 });

    revealItems.forEach((item) => revealObserver.observe(item));
  }

  document.querySelectorAll('[data-year]').forEach((item) => {
    item.textContent = String(new Date().getFullYear());
  });
})();
