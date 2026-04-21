// ARM CPU Emulator Cookbook - Main JS

(function() {
    'use strict';

    // ========== NAVIGATION ==========
    const sidebar = document.querySelector('.sidebar');
    const menuToggle = document.querySelector('.menu-toggle');

    if (menuToggle && sidebar) {
        menuToggle.addEventListener('click', () => {
            sidebar.classList.toggle('open');
        });

        // Close sidebar on outside click (mobile)
        document.addEventListener('click', (e) => {
            if (window.innerWidth <= 768 &&
                sidebar.classList.contains('open') &&
                !sidebar.contains(e.target) &&
                !menuToggle.contains(e.target)) {
                sidebar.classList.remove('open');
            }
        });
    }

    // Set active nav item based on current page
    function setActiveNav() {
        const path = window.location.pathname;
        const navItems = document.querySelectorAll('.nav-item');
        navItems.forEach(item => {
            const href = item.getAttribute('href');
            if (href && path.endsWith(href.replace(/^\.\.\//, ''))) {
                item.classList.add('active');
            } else {
                item.classList.remove('active');
            }
        });
    }
    setActiveNav();

    // ========== SEARCH ==========
    const searchInput = document.querySelector('.header-search input');
    const searchResults = document.querySelector('.search-results');

    if (searchInput && searchResults) {
        // Build search index from all pages
        let searchIndex = [];

        async function buildSearchIndex() {
            const pages = [
                // Part 1
                { title: 'Quick Start', section: 'Usage Guide', url: 'part1-usage/01-quick-start.html', keywords: 'install build cmake compile run first setup environment requirements' },
                { title: 'CLI Reference', section: 'Usage Guide', url: 'part1-usage/02-cli-reference.html', keywords: 'command line arguments parameters options flags' },
                { title: 'Input Formats', section: 'Usage Guide', url: 'part1-usage/03-input-formats.html', keywords: 'elf text trace champsim json format file input' },
                { title: 'Writing Test Programs', section: 'Usage Guide', url: 'part1-usage/04-writing-test-programs.html', keywords: 'arm64 assembly program compile cross-compiler aarch64 instructions' },
                { title: 'Running Simulations', section: 'Usage Guide', url: 'part1-usage/05-running-simulations.html', keywords: 'run execute simulation visualize web server event mode' },
                { title: 'Output & Analysis', section: 'Usage Guide', url: 'part1-usage/06-output-analysis.html', keywords: 'json output results ipc cache hit rate performance metrics analysis' },
                { title: 'Visualization', section: 'Usage Guide', url: 'part1-usage/07-visualization.html', keywords: 'konata pipeline timeline viewer web server' },
                { title: 'Configuration Presets', section: 'Usage Guide', url: 'part1-usage/08-configuration-presets.html', keywords: 'config preset default high-performance minimal gem5' },
                { title: 'Benchmarks', section: 'Usage Guide', url: 'part1-usage/09-benchmarks.html', keywords: 'benchmark dhrystone fibonacci matmul quicksort linkedlist memcpy' },
                { title: 'Automation Scripts', section: 'Usage Guide', url: 'part1-usage/10-automation-scripts.html', keywords: 'scripts build test compile automation ci cd' },
                { title: 'FAQ & Troubleshooting', section: 'Usage Guide', url: 'part1-usage/11-faq.html', keywords: 'faq troubleshooting error problem fix debug' },
                // Part 2
                { title: 'System Overview', section: 'Architecture', url: 'part2-architecture/01-system-overview.html', keywords: 'architecture overview modules structure diagram' },
                { title: 'CPU Pipeline', section: 'Architecture', url: 'part2-architecture/02-cpu-pipeline.html', keywords: 'pipeline fetch decode rename dispatch issue execute commit' },
                { title: 'Out-of-Order Engine', section: 'Architecture', url: 'part2-architecture/03-ooo-engine.html', keywords: 'ooo out-of-order engine window dependency scheduler reorder rob' },
                { title: 'Instruction Decoder', section: 'Architecture', url: 'part2-architecture/04-instruction-decoder.html', keywords: 'decoder capstone aarch64 arm64 disassembly mnemonic' },
                { title: 'Memory Hierarchy', section: 'Architecture', url: 'part2-architecture/05-memory-hierarchy.html', keywords: 'cache memory l1 l2 l3 ddr lsq load store' },
                { title: 'ELF Loader', section: 'Architecture', url: 'part2-architecture/06-elf-loader.html', keywords: 'elf loader parser segment symbol memory map' },
                { title: 'Statistics', section: 'Architecture', url: 'part2-architecture/07-statistics.html', keywords: 'stats statistics metrics collector performance ipc cpi' },
                { title: 'Visualization System', section: 'Architecture', url: 'part2-architecture/08-visualization-system.html', keywords: 'visualization konata pipeline tracker state json export' },
                { title: 'CHI Coherence', section: 'Architecture', url: 'part2-architecture/09-chi-coherence.html', keywords: 'chi coherence amba protocol rnf hnf snf directory' },
                { title: 'Type System', section: 'Architecture', url: 'part2-architecture/10-type-system.html', keywords: 'types register instruction opcode result error' },
                { title: 'Input Sources', section: 'Architecture', url: 'part2-architecture/11-input-sources.html', keywords: 'input source trace elf functional simulation' },
                { title: 'GEM5 Comparison', section: 'Architecture', url: 'part2-architecture/12-gem5-comparison.html', keywords: 'gem5 comparison compare o3 arm v7a' },
            ];
            searchIndex = pages;
        }

        buildSearchIndex();

        searchInput.addEventListener('input', () => {
            const query = searchInput.value.trim().toLowerCase();
            if (query.length < 2) {
                searchResults.classList.remove('active');
                return;
            }

            const matches = searchIndex.filter(p => {
                return p.title.toLowerCase().includes(query) ||
                       p.section.toLowerCase().includes(query) ||
                       p.keywords.toLowerCase().includes(query);
            });

            if (matches.length === 0) {
                searchResults.innerHTML = '<div style="padding:12px;color:#6b7280;font-size:0.875rem;">No results found</div>';
            } else {
                searchResults.innerHTML = matches.map(p => {
                    const url = p.url;
                    return `<a href="${url}">
                        <div class="search-title">${highlightMatch(p.title, query)}</div>
                        <div class="search-section">${p.section}</div>
                    </a>`;
                }).join('');
            }

            searchResults.classList.add('active');
        });

        searchInput.addEventListener('blur', () => {
            setTimeout(() => searchResults.classList.remove('active'), 200);
        });

        searchInput.addEventListener('focus', () => {
            if (searchInput.value.trim().length >= 2) {
                searchInput.dispatchEvent(new Event('input'));
            }
        });

        function highlightMatch(text, query) {
            const idx = text.toLowerCase().indexOf(query);
            if (idx === -1) return text;
            return text.slice(0, idx) +
                   '<strong>' + text.slice(idx, idx + query.length) + '</strong>' +
                   text.slice(idx + query.length);
        }
    }

    // ========== CODE BLOCK COPY ==========
    document.addEventListener('click', (e) => {
        if (e.target.classList.contains('copy-btn')) {
            const pre = e.target.closest('.code-block-wrapper')?.querySelector('pre code') ||
                        e.target.previousElementSibling?.querySelector('code');
            if (pre) {
                navigator.clipboard.writeText(pre.textContent).then(() => {
                    const orig = e.target.textContent;
                    e.target.textContent = 'Copied!';
                    setTimeout(() => e.target.textContent = orig, 1500);
                });
            }
        }
    });

    // ========== SMOOTH SCROLL FOR ANCHORS ==========
    document.querySelectorAll('a[href^="#"]').forEach(a => {
        a.addEventListener('click', (e) => {
            const target = document.querySelector(a.getAttribute('href'));
            if (target) {
                e.preventDefault();
                target.scrollIntoView({ behavior: 'smooth', block: 'start' });
            }
        });
    });

    // ========== COLLAPSIBLE SECTIONS ==========
    document.querySelectorAll('.collapsible-header').forEach(header => {
        header.addEventListener('click', () => {
            const content = header.nextElementSibling;
            if (content) {
                const isHidden = content.style.display === 'none';
                content.style.display = isHidden ? 'block' : 'none';
                header.querySelector('.collapse-icon').textContent = isHidden ? '▼' : '▶';
            }
        });
    });

    // ========== KEYBOARD SHORTCUTS ==========
    document.addEventListener('keydown', (e) => {
        // Cmd/Ctrl + K to focus search
        if ((e.metaKey || e.ctrlKey) && e.key === 'k') {
            e.preventDefault();
            if (searchInput) searchInput.focus();
        }
        // Escape to close sidebar on mobile
        if (e.key === 'Escape' && window.innerWidth <= 768) {
            if (sidebar) sidebar.classList.remove('open');
            if (searchResults) searchResults.classList.remove('active');
        }
    });

})();
