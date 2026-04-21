// Shared sidebar template generator
// Usage: Include this before main.js, then call buildSidebar(partNum) in each page

function sidebarHTML(activePath) {
    const items = {
        'part1': [
            { num: '1', title: 'Quick Start', href: 'part1-usage/01-quick-start.html' },
            { num: '2', title: 'CLI Reference', href: 'part1-usage/02-cli-reference.html' },
            { num: '3', title: 'Input Formats', href: 'part1-usage/03-input-formats.html' },
            { num: '4', title: 'Writing Test Programs', href: 'part1-usage/04-writing-test-programs.html' },
            { num: '5', title: 'Running Simulations', href: 'part1-usage/05-running-simulations.html' },
            { num: '6', title: 'Output & Analysis', href: 'part1-usage/06-output-analysis.html' },
            { num: '7', title: 'Visualization', href: 'part1-usage/07-visualization.html' },
            { num: '8', title: 'Configuration Presets', href: 'part1-usage/08-configuration-presets.html' },
            { num: '9', title: 'Benchmarks', href: 'part1-usage/09-benchmarks.html' },
            { num: '10', title: 'Automation Scripts', href: 'part1-usage/10-automation-scripts.html' },
            { num: '11', title: 'FAQ & Troubleshooting', href: 'part1-usage/11-faq.html' },
        ],
        'part2': [
            { num: '1', title: 'System Overview', href: 'part2-architecture/01-system-overview.html' },
            { num: '2', title: 'CPU Pipeline', href: 'part2-architecture/02-cpu-pipeline.html' },
            { num: '3', title: 'Out-of-Order Engine', href: 'part2-architecture/03-ooo-engine.html' },
            { num: '4', title: 'Instruction Decoder', href: 'part2-architecture/04-instruction-decoder.html' },
            { num: '5', title: 'Memory Hierarchy', href: 'part2-architecture/05-memory-hierarchy.html' },
            { num: '6', title: 'ELF Loader', href: 'part2-architecture/06-elf-loader.html' },
            { num: '7', title: 'Statistics', href: 'part2-architecture/07-statistics.html' },
            { num: '8', title: 'Visualization System', href: 'part2-architecture/08-visualization-system.html' },
            { num: '9', title: 'CHI Coherence', href: 'part2-architecture/09-chi-coherence.html' },
            { num: '10', title: 'Type System', href: 'part2-architecture/10-type-system.html' },
            { num: '11', title: 'Input Sources', href: 'part2-architecture/11-input-sources.html' },
            { num: '12', title: 'GEM5 Comparison', href: 'part2-architecture/12-gem5-comparison.html' },
        ]
    };

    function renderSection(title, sectionItems) {
        const links = sectionItems.map(i => {
            const isActive = activePath.endsWith(i.href);
            return `<a class="nav-item${isActive ? ' active' : ''}" href="../${i.href}">${i.num}. ${i.title}</a>`;
        }).join('\n        ');
        return `    <div class="nav-section">
        <div class="nav-section-title">${title}</div>
        ${links}
    </div>`;
    }

    return `${renderSection('Part 1 &mdash; Usage Guide', items.part1)}
${renderSection('Part 2 &mdash; Architecture', items.part2)}`;
}
