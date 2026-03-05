/* ============================================================
   The Story of ms-os -- shared.js
   Vanilla JS. No frameworks. No emojis.
   Handles: scroll progress, back-to-top, mobile nav, dropdown.
   ============================================================ */

(function () {
  'use strict';

  /* --- Scroll Progress Bar --- */
  var progressBar = document.getElementById('scroll-progress');
  if (progressBar) {
    window.addEventListener('scroll', function () {
      var scrollTop = window.scrollY || document.documentElement.scrollTop;
      var docHeight = document.documentElement.scrollHeight - window.innerHeight;
      var progress = docHeight > 0 ? (scrollTop / docHeight) * 100 : 0;
      progressBar.style.width = Math.min(progress, 100) + '%';
    }, { passive: true });
  }

  /* --- Back to Top Button --- */
  var backToTop = document.getElementById('back-to-top');
  if (backToTop) {
    var threshold = 300;

    window.addEventListener('scroll', function () {
      var scrollTop = window.scrollY || document.documentElement.scrollTop;
      if (scrollTop > threshold) {
        backToTop.classList.add('visible');
      } else {
        backToTop.classList.remove('visible');
      }
    }, { passive: true });

    backToTop.addEventListener('click', function () {
      window.scrollTo({ top: 0, behavior: 'smooth' });
    });
  }

  /* --- Chapter Dropdown Toggle --- */
  var dropdownToggle = document.querySelector('.nav-dropdown-toggle');
  var dropdownMenu = document.querySelector('.nav-dropdown-menu');
  if (dropdownToggle && dropdownMenu) {
    dropdownToggle.addEventListener('click', function (e) {
      e.stopPropagation();
      dropdownMenu.classList.toggle('open');
    });

    document.addEventListener('click', function () {
      dropdownMenu.classList.remove('open');
    });

    dropdownMenu.addEventListener('click', function (e) {
      e.stopPropagation();
    });
  }

  /* --- Mobile Hamburger Toggle --- */
  var hamburger = document.querySelector('.nav-hamburger');
  var mobilePanel = document.querySelector('.mobile-nav-panel');
  if (hamburger && mobilePanel) {
    hamburger.addEventListener('click', function () {
      var isOpen = mobilePanel.classList.toggle('open');
      hamburger.textContent = isOpen ? 'X' : '=';
      document.body.style.overflow = isOpen ? 'hidden' : '';
    });
  }

  /* --- Subtle scroll-triggered fade-in --- */
  var fadeElements = document.querySelectorAll('.fade-in-on-scroll');
  if (fadeElements.length > 0 && 'IntersectionObserver' in window) {
    var observer = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) {
        if (entry.isIntersecting) {
          entry.target.classList.add('visible');
          observer.unobserve(entry.target);
        }
      });
    }, { threshold: 0.1, rootMargin: '0px 0px -40px 0px' });

    fadeElements.forEach(function (el) {
      observer.observe(el);
    });
  }

  /* --- Left Sidebar Navigation (auto-generated) --- */
  (function () {
    var content = document.querySelector('.content');
    var dropdownLinks = document.querySelectorAll('.nav-dropdown-menu a');
    if (!content || dropdownLinks.length === 0) return;

    var h2s = content.querySelectorAll('h2');

    // Only show sidebar if there are chapters or sections to navigate
    if (dropdownLinks.length === 0 && h2s.length === 0) return;

    // Slugify a heading text into a URL-friendly id
    function slugify(text) {
      return text.toLowerCase()
        .replace(/[^a-z0-9\s-]/g, '')
        .replace(/\s+/g, '-')
        .replace(/-+/g, '-')
        .replace(/^-|-$/g, '');
    }

    // Ensure all h2s have ids
    for (var i = 0; i < h2s.length; i++) {
      if (!h2s[i].id) {
        h2s[i].id = slugify(h2s[i].textContent);
      }
    }

    // Build the sidebar element
    var sidebar = document.createElement('aside');
    sidebar.className = 'sidebar';

    // --- Chapters section (collapsible, collapsed by default) ---
    var chapTitle = document.createElement('div');
    chapTitle.className = 'sidebar-section-title collapsible';
    chapTitle.textContent = 'Chapters';
    sidebar.appendChild(chapTitle);

    var chapList = document.createElement('ul');
    chapList.className = 'sidebar-section collapsed';

    for (var j = 0; j < dropdownLinks.length; j++) {
      var li = document.createElement('li');
      var a = document.createElement('a');
      a.href = dropdownLinks[j].href;
      a.textContent = dropdownLinks[j].textContent;
      if (dropdownLinks[j].classList.contains('current')) {
        a.className = 'current';
      }
      li.appendChild(a);
      chapList.appendChild(li);
    }
    sidebar.appendChild(chapList);

    chapTitle.addEventListener('click', function () {
      chapTitle.classList.toggle('expanded');
      chapList.classList.toggle('collapsed');
    });

    // --- On This Page section (h2 links) ---
    if (h2s.length > 0) {
      var secTitle = document.createElement('div');
      secTitle.className = 'sidebar-section-title';
      secTitle.textContent = 'On This Page';
      sidebar.appendChild(secTitle);

      var secList = document.createElement('ul');
      secList.className = 'sidebar-section';

      for (var k = 0; k < h2s.length; k++) {
        var sli = document.createElement('li');
        var sa = document.createElement('a');
        sa.href = '#' + h2s[k].id;
        sa.textContent = h2s[k].textContent;
        sa.setAttribute('data-section-index', k);
        sli.appendChild(sa);
        secList.appendChild(sli);
      }
      sidebar.appendChild(secList);
    }

    // Insert sidebar into DOM
    content.parentNode.insertBefore(sidebar, content);
    document.body.classList.add('has-sidebar');

    // --- Smooth scroll with header offset ---
    var headerOffset = 70; // header + progress bar + breathing room
    var sectionLinks = sidebar.querySelectorAll('.sidebar-section a[href^="#"]');
    for (var m = 0; m < sectionLinks.length; m++) {
      sectionLinks[m].addEventListener('click', function (e) {
        var targetId = this.getAttribute('href').slice(1);
        var target = document.getElementById(targetId);
        if (target) {
          e.preventDefault();
          var top = target.getBoundingClientRect().top + window.scrollY - headerOffset;
          window.scrollTo({ top: top, behavior: 'smooth' });
        }
      });
    }

    // --- Scroll spy ---
    if (h2s.length > 0 && secList) {
      var spyLinks = secList.querySelectorAll('a');

      function updateActiveSpy() {
        var scrollPos = window.scrollY + headerOffset + 20;
        var activeIndex = -1;

        for (var n = h2s.length - 1; n >= 0; n--) {
          if (h2s[n].offsetTop <= scrollPos) {
            activeIndex = n;
            break;
          }
        }

        for (var p = 0; p < spyLinks.length; p++) {
          if (p === activeIndex) {
            spyLinks[p].classList.add('active');
          } else {
            spyLinks[p].classList.remove('active');
          }
        }

        // Auto-scroll sidebar to keep active link visible
        if (activeIndex >= 0) {
          var activeLink = spyLinks[activeIndex];
          var sidebarRect = sidebar.getBoundingClientRect();
          var linkRect = activeLink.getBoundingClientRect();
          if (linkRect.top < sidebarRect.top || linkRect.bottom > sidebarRect.bottom) {
            activeLink.scrollIntoView({ block: 'nearest', behavior: 'smooth' });
          }
        }
      }

      window.addEventListener('scroll', updateActiveSpy, { passive: true });
      updateActiveSpy(); // initial state
    }
  })();

})();
