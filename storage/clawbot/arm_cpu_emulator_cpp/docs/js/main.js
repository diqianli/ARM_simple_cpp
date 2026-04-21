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
                // Chinese pages
                { title: '快速开始', section: '使用指南 (中文)', url: 'zh/part1-usage/01-quick-start.html', keywords: '安装 构建 cmake 编译 运行 环境 依赖 交叉编译 aarch64' },
                { title: 'CLI 参数参考', section: '使用指南 (中文)', url: 'zh/part1-usage/02-cli-reference.html', keywords: '命令行 参数 选项 标志 配置 窗口 发射 缓存' },
                { title: '输入格式', section: '使用指南 (中文)', url: 'zh/part1-usage/03-input-formats.html', keywords: 'elf text trace champsim json 输入 格式 文件' },
                { title: '编写测试程序', section: '使用指南 (中文)', url: 'zh/part1-usage/04-writing-test-programs.html', keywords: 'arm64 汇编 编译 交叉编译 测试 裸机 程序 指令' },
                { title: '运行仿真', section: '使用指南 (中文)', url: 'zh/part1-usage/05-running-simulations.html', keywords: '运行 仿真 可视化 服务器 事件 多实例 并行' },
                { title: '输出与结果分析', section: '使用指南 (中文)', url: 'zh/part1-usage/06-output-analysis.html', keywords: '输出 结果 json ipc 缓存 命中率 性能 指标 分析' },
                { title: '可视化工具', section: '使用指南 (中文)', url: 'zh/part1-usage/07-visualization.html', keywords: 'konata 流水线 时间轴 可视化 查看器 网页' },
                { title: '配置预设', section: '使用指南 (中文)', url: 'zh/part1-usage/08-configuration-presets.html', keywords: '配置 预设 默认 高性能 最小 gem5 参数' },
                { title: '基准测试', section: '使用指南 (中文)', url: 'zh/part1-usage/09-benchmarks.html', keywords: '基准 测试 dhrystone fibonacci 矩阵 快速排序 链表 memcpy' },
                { title: '自动化脚本', section: '使用指南 (中文)', url: 'zh/part1-usage/10-automation-scripts.html', keywords: '脚本 构建 测试 编译 自动化 ci' },
                { title: '常见问题与排错', section: '使用指南 (中文)', url: 'zh/part1-usage/11-faq.html', keywords: '常见问题 排错 错误 调试 修复 故障' },
                { title: '系统概览', section: '架构文档 (中文)', url: 'zh/part2-architecture/01-system-overview.html', keywords: '系统 概览 模块 结构 架构 目录 依赖' },
                { title: 'CPU 流水线', section: '架构文档 (中文)', url: 'zh/part2-architecture/02-cpu-pipeline.html', keywords: '流水线 取指 解码 重命名 分派 发射 执行 提交' },
                { title: '乱序执行引擎', section: '架构文档 (中文)', url: 'zh/part2-architecture/03-ooo-engine.html', keywords: '乱序 ooo 引擎 窗口 依赖 调度 重排序 rob' },
                { title: '指令解码器', section: '架构文档 (中文)', url: 'zh/part2-architecture/04-instruction-decoder.html', keywords: '解码器 capstone aarch64 arm64 反汇编 助记符' },
                { title: '内存层次结构', section: '架构文档 (中文)', url: 'zh/part2-architecture/05-memory-hierarchy.html', keywords: '缓存 内存 l1 l2 l3 ddr lsq 加载 存储' },
                { title: 'ELF 加载器', section: '架构文档 (中文)', url: 'zh/part2-architecture/06-elf-loader.html', keywords: 'elf 加载器 解析 段 符号 内存映射' },
                { title: '统计收集系统', section: '架构文档 (中文)', url: 'zh/part2-architecture/07-statistics.html', keywords: '统计 指标 收集 性能 ipc cpi 延迟' },
                { title: '可视化系统', section: '架构文档 (中文)', url: 'zh/part2-architecture/08-visualization-system.html', keywords: '可视化 konata 流水线 跟踪 状态 json 导出' },
                { title: 'CHI 一致性协议', section: '架构文档 (中文)', url: 'zh/part2-architecture/09-chi-coherence.html', keywords: 'chi 一致性 amba 协议 rnf hnf snf 目录' },
                { title: '类型系统与数据结构', section: '架构文档 (中文)', url: 'zh/part2-architecture/10-type-system.html', keywords: '类型 寄存器 指令 操作码 结果 错误' },
                { title: '输入源接口', section: '架构文档 (中文)', url: 'zh/part2-architecture/11-input-sources.html', keywords: '输入 源 接口 trace elf 功能仿真' },
                { title: '与 GEM5 对比', section: '架构文档 (中文)', url: 'zh/part2-architecture/12-gem5-comparison.html', keywords: 'gem5 对比 比较 o3 arm v7a' },
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
