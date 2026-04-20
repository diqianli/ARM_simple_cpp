// Performance Statistics View — Chart.js rendering for ARM CPU Emulator
// Renders 8 charts + summary cards + detailed metrics table from perf stats JSON

class PerfStatsView {
    constructor() {
        this.charts = {};
        this.data = null;
        this.benchName = '';

        // Color constants (matching perf_profiling.html palette)
        this.BLUE = '#58a6ff';
        this.PINK = '#f97583';
        this.GREEN = '#3fb950';
        this.ORANGE = '#d29922';
        this.PURPLE = '#bc8cff';
        this.CYAN = '#56d4dd';
        this.TICK = '#8b949e';
        this.GRID = 'rgba(139,148,158,0.15)';

        this.BLUE_BG = 'rgba(88,166,255,0.65)';
        this.PINK_BG = 'rgba(249,117,131,0.65)';
        this.GREEN_BG = 'rgba(63,185,80,0.65)';
        this.ORANGE_BG = 'rgba(210,153,34,0.65)';
        this.PURPLE_BG = 'rgba(188,140,255,0.65)';
        this.CYAN_BG = 'rgba(86,212,221,0.65)';

        // DOM elements
        this.elements = {
            statsContent: document.getElementById('statsContent'),
            statsCards: document.getElementById('statsCards'),
            statsTableBody: document.getElementById('statsTableBody'),
            statsFileInput: document.getElementById('statsFileInput')
        };

        this._initChartDefaults();
        this._initFileInput();
    }

    _initChartDefaults() {
        if (typeof Chart === 'undefined') return;
        Chart.defaults.color = this.TICK;
        Chart.defaults.borderColor = this.GRID;
        Chart.defaults.font.family = "-apple-system, BlinkMacSystemFont, 'Segoe UI', Helvetica, sans-serif";
        Chart.defaults.font.size = 12;
    }

    _initFileInput() {
        if (this.elements.statsFileInput) {
            this.elements.statsFileInput.addEventListener('change', (e) => {
                const file = e.target.files[0];
                if (file) this.loadFile(file);
            });
        }
    }

    _commonPlugins() {
        return {
            legend: {
                labels: { color: '#e6edf3', boxWidth: 14, padding: 16 }
            },
            tooltip: {
                backgroundColor: '#1c2128',
                borderColor: '#30363d',
                borderWidth: 1,
                titleColor: '#e6edf3',
                bodyColor: '#e6edf3',
                padding: 10
            }
        };
    }

    loadFile(file) {
        const reader = new FileReader();
        reader.onload = (e) => {
            try {
                const data = JSON.parse(e.target.result);
                // Detect benchmark name from filename or from data
                const name = file.name.replace(/\.json$/, '').replace(/_perf$/, '');
                this.render(data, name);
            } catch (err) {
                console.error('Failed to parse perf stats JSON:', err);
                if (window.staticViz) {
                    window.staticViz.updateStatus('Error: Failed to parse performance stats JSON - ' + err.message);
                }
            }
        };
        reader.onerror = () => {
            if (window.staticViz) {
                window.staticViz.updateStatus('Error reading performance stats file');
            }
        };
        reader.readAsText(file);
    }

    render(data, benchName) {
        this.data = data;
        this.benchName = benchName || 'Simulation';

        // Destroy existing charts
        Object.values(this.charts).forEach(c => { if (c) c.destroy(); });
        this.charts = {};

        // Show stats content
        if (this.elements.statsContent) {
            this.elements.statsContent.style.display = 'block';
        }

        this._renderCards(data);
        this._renderInstrMix(data);
        this._renderBranchAccuracy(data);
        this._renderBtbRas(data);
        this._renderCache(data);
        this._renderStalls(data);
        this._renderFU(data);
        this._renderIssueWidth(data);
        this._renderIPC(data);
        this._renderTable(data);
    }

    // ===================== SUMMARY CARDS =====================
    _renderCards(d) {
        const bp = d.branch_predictor || {};
        const stalls = d.pipeline_stalls || {};
        const cache = d.cache_detail || {};

        const ipc = d.ipc || 0;
        const cpi = d.cpi || 0;
        const totalCycles = d.total_cycles || 1;
        const mispredRate = bp.total_branches ? (bp.mispredictions / bp.total_branches * 100) : 0;
        const stallRate = stalls.total_stall_cycles ? (stalls.total_stall_cycles / totalCycles * 100) : 0;

        const l1ReadMisses = (cache.l1?.read_misses || 0) + (cache.l1?.write_misses || 0);
        const l2Misses = (cache.l2?.read_misses || 0) + (cache.l2?.write_misses || 0);

        if (this.elements.statsCards) {
            this.elements.statsCards.innerHTML = `
                <div class="stats-card c-ipc">
                    <div class="stats-card-label">IPC / CPI</div>
                    <div class="stats-card-value">${ipc.toFixed(3)} / ${cpi.toFixed(3)}</div>
                    <div class="stats-card-sub">${(d.total_instructions || 0).toLocaleString()} instructions in ${(d.total_cycles || 0).toLocaleString()} cycles</div>
                </div>
                <div class="stats-card c-mispred">
                    <div class="stats-card-label">Branch Mispred Rate</div>
                    <div class="stats-card-value">${mispredRate.toFixed(1)}%</div>
                    <div class="stats-card-sub">${(bp.mispredictions || 0)} mispredictions out of ${(bp.total_branches || 0)} branches</div>
                </div>
                <div class="stats-card c-miss">
                    <div class="stats-card-label">Cache Misses</div>
                    <div class="stats-card-value">${l1ReadMisses + l2Misses}</div>
                    <div class="stats-card-sub">L1: ${l1ReadMisses} misses, L2: ${l2Misses} misses</div>
                </div>
                <div class="stats-card c-stall">
                    <div class="stats-card-label">Stall Rate</div>
                    <div class="stats-card-value">${stallRate.toFixed(1)}%</div>
                    <div class="stats-card-sub">${(stalls.total_stall_cycles || 0)} stall cycles / ${totalCycles} total</div>
                </div>
            `;
        }
    }

    // ===================== INSTRUCTION MIX (DONUT) =====================
    _categorizeInstr(instrMap) {
        const cats = { 'Int ALU': 0, 'Load/Store': 0, 'Branch': 0, 'Other': 0 };
        if (!instrMap) return cats;
        for (const [op, count] of Object.entries(instrMap)) {
            if (['ADD','SUB','MUL','AND','ORR','EOR','LSL','LSR','ASR','MOV','CSEL','CMP','NOP'].includes(op)) {
                cats['Int ALU'] += count;
            } else if (['LDR','STR','LDP','STP','LDUR','STUR'].includes(op)) {
                cats['Load/Store'] += count;
            } else if (['B','B.cond','BR','BL','CBZ','CBNZ','TBZ','TBNZ','RET'].includes(op)) {
                cats['Branch'] += count;
            } else {
                cats['Other'] += count;
            }
        }
        return cats;
    }

    _renderInstrMix(d) {
        const ctx = document.getElementById('chartInstrMix');
        if (!ctx || typeof Chart === 'undefined') return;

        const cats = this._categorizeInstr(d.instructions);
        this.charts.instrMix = new Chart(ctx, {
            type: 'doughnut',
            data: {
                labels: Object.keys(cats),
                datasets: [{
                    data: Object.values(cats),
                    backgroundColor: [this.BLUE_BG, this.GREEN_BG, this.ORANGE_BG, this.PURPLE_BG],
                    borderColor: [this.BLUE, this.GREEN, this.ORANGE, this.PURPLE],
                    borderWidth: 1.5
                }]
            },
            options: {
                responsive: true,
                cutout: '55%',
                plugins: {
                    ...this._commonPlugins(),
                    legend: { ...this._commonPlugins().legend, position: 'right' },
                    title: {
                        display: true,
                        text: `${this.benchName} (${(d.total_instructions || 0).toLocaleString()} instructions)`,
                        color: '#e6edf3',
                        font: { size: 13, weight: 600 }
                    }
                }
            }
        });
    }

    // ===================== BRANCH PREDICTION ACCURACY =====================
    _renderBranchAccuracy(d) {
        const ctx = document.getElementById('chartBranchAcc');
        if (!ctx || typeof Chart === 'undefined') return;

        const bp = d.branch_predictor || {};
        this.charts.branchAcc = new Chart(ctx, {
            type: 'bar',
            data: {
                labels: [this.benchName],
                datasets: [
                    {
                        label: 'Correct',
                        data: [bp.correct || 0],
                        backgroundColor: this.GREEN_BG,
                        borderColor: this.GREEN,
                        borderWidth: 1
                    },
                    {
                        label: 'Mispredictions',
                        data: [bp.mispredictions || 0],
                        backgroundColor: this.PINK_BG,
                        borderColor: this.PINK,
                        borderWidth: 1
                    }
                ]
            },
            options: {
                responsive: true,
                scales: {
                    x: { grid: { display: false }, ticks: { color: this.TICK }, stacked: true },
                    y: { grid: { color: this.GRID }, ticks: { color: this.TICK }, stacked: true, title: { display: true, text: 'Branches', color: this.TICK } }
                },
                plugins: this._commonPlugins()
            }
        });
    }

    // ===================== BTB / RAS =====================
    _renderBtbRas(d) {
        const ctx = document.getElementById('chartBtbRas');
        if (!ctx || typeof Chart === 'undefined') return;

        const bp = d.branch_predictor || {};
        this.charts.btbRas = new Chart(ctx, {
            type: 'bar',
            data: {
                labels: [this.benchName],
                datasets: [
                    { label: 'BTB Hits', data: [bp.btb_hits || 0], backgroundColor: this.BLUE_BG, borderColor: this.BLUE, borderWidth: 1 },
                    { label: 'BTB Misses', data: [bp.btb_misses || 0], backgroundColor: 'rgba(88,166,255,0.2)', borderColor: this.BLUE, borderWidth: 1 },
                    { label: 'RAS Hits', data: [bp.ras_hits || 0], backgroundColor: this.GREEN_BG, borderColor: this.GREEN, borderWidth: 1 },
                    { label: 'RAS Misses', data: [bp.ras_misses || 0], backgroundColor: 'rgba(63,185,80,0.2)', borderColor: this.GREEN, borderWidth: 1 }
                ]
            },
            options: {
                responsive: true,
                scales: {
                    x: { grid: { display: false }, ticks: { color: this.TICK } },
                    y: { grid: { color: this.GRID }, ticks: { color: this.TICK }, title: { display: true, text: 'Count', color: this.TICK } }
                },
                plugins: this._commonPlugins()
            }
        });
    }

    // ===================== CACHE HIERARCHY =====================
    _renderCache(d) {
        const ctx = document.getElementById('chartCache');
        if (!ctx || typeof Chart === 'undefined') return;

        const cache = d.cache_detail || {};
        const l1 = cache.l1 || {};
        const l2 = cache.l2 || {};
        this.charts.cache = new Chart(ctx, {
            type: 'bar',
            data: {
                labels: [this.benchName],
                datasets: [
                    { label: 'L1 Reads', data: [l1.reads || 0], backgroundColor: this.BLUE_BG, borderColor: this.BLUE, borderWidth: 1 },
                    { label: 'L1 Writes', data: [l1.writes || 0], backgroundColor: this.GREEN_BG, borderColor: this.GREEN, borderWidth: 1 },
                    { label: 'L1 Read Misses', data: [l1.read_misses || 0], backgroundColor: this.PINK_BG, borderColor: this.PINK, borderWidth: 1 },
                    { label: 'L1 Write Misses', data: [l1.write_misses || 0], backgroundColor: this.ORANGE_BG, borderColor: this.ORANGE, borderWidth: 1 },
                    { label: 'L2 Reads', data: [l2.reads || 0], backgroundColor: this.PURPLE_BG, borderColor: this.PURPLE, borderWidth: 1 },
                    { label: 'L2 Writes', data: [l2.writes || 0], backgroundColor: this.CYAN_BG, borderColor: this.CYAN, borderWidth: 1 }
                ]
            },
            options: {
                responsive: true,
                scales: {
                    x: { grid: { display: false }, ticks: { color: this.TICK } },
                    y: { grid: { color: this.GRID }, ticks: { color: this.TICK }, title: { display: true, text: 'Operations', color: this.TICK } }
                },
                plugins: this._commonPlugins()
            }
        });
    }

    // ===================== PIPELINE STALLS (HORIZONTAL STACKED) =====================
    _renderStalls(d) {
        const ctx = document.getElementById('chartStalls');
        if (!ctx || typeof Chart === 'undefined') return;

        const stalls = d.pipeline_stalls || {};
        this.charts.stalls = new Chart(ctx, {
            type: 'bar',
            data: {
                labels: [this.benchName],
                datasets: [
                    { label: 'ROB Full', data: [stalls.rob_full || 0], backgroundColor: this.BLUE_BG, borderColor: this.BLUE, borderWidth: 1 },
                    { label: 'IQ Full', data: [stalls.iq_full || 0], backgroundColor: this.GREEN_BG, borderColor: this.GREEN, borderWidth: 1 },
                    { label: 'LSQ Full', data: [stalls.lsq_full || 0], backgroundColor: this.PURPLE_BG, borderColor: this.PURPLE, borderWidth: 1 },
                    { label: 'Cache Miss', data: [stalls.cache_miss || 0], backgroundColor: this.PINK_BG, borderColor: this.PINK, borderWidth: 1 },
                    { label: 'Branch Mispredict', data: [stalls.branch_mispredict || 0], backgroundColor: this.ORANGE_BG, borderColor: this.ORANGE, borderWidth: 1 }
                ]
            },
            options: {
                responsive: true,
                indexAxis: 'y',
                scales: {
                    x: { grid: { color: this.GRID }, ticks: { color: this.TICK }, stacked: true, title: { display: true, text: 'Stall Cycles', color: this.TICK } },
                    y: { grid: { display: false }, ticks: { color: this.TICK }, stacked: true }
                },
                plugins: this._commonPlugins()
            }
        });
    }

    // ===================== FU UTILIZATION (STACKED BAR) =====================
    _renderFU(d) {
        const ctx = document.getElementById('chartFU');
        if (!ctx || typeof Chart === 'undefined') return;

        const fu = d.fu_utilization || {};
        this.charts.fu = new Chart(ctx, {
            type: 'bar',
            data: {
                labels: [this.benchName],
                datasets: [
                    { label: 'Int ALU', data: [fu.int_alu_busy_cycles || 0], backgroundColor: this.BLUE_BG, borderColor: this.BLUE, borderWidth: 1 },
                    { label: 'Int MUL', data: [fu.int_mul_busy_cycles || 0], backgroundColor: this.CYAN_BG, borderColor: this.CYAN, borderWidth: 1 },
                    { label: 'Load', data: [fu.load_busy_cycles || 0], backgroundColor: this.GREEN_BG, borderColor: this.GREEN, borderWidth: 1 },
                    { label: 'Store', data: [fu.store_busy_cycles || 0], backgroundColor: this.ORANGE_BG, borderColor: this.ORANGE, borderWidth: 1 },
                    { label: 'Branch', data: [fu.branch_busy_cycles || 0], backgroundColor: this.PINK_BG, borderColor: this.PINK, borderWidth: 1 },
                    { label: 'FP/SIMD', data: [fu.fp_simd_busy_cycles || 0], backgroundColor: this.PURPLE_BG, borderColor: this.PURPLE, borderWidth: 1 }
                ]
            },
            options: {
                responsive: true,
                scales: {
                    x: { grid: { display: false }, ticks: { color: this.TICK }, stacked: true },
                    y: { grid: { color: this.GRID }, ticks: { color: this.TICK }, stacked: true, title: { display: true, text: 'Busy Cycles', color: this.TICK } }
                },
                plugins: this._commonPlugins()
            }
        });
    }

    // ===================== ISSUE WIDTH DISTRIBUTION =====================
    _renderIssueWidth(d) {
        const ctx = document.getElementById('chartIssueWidth');
        if (!ctx || typeof Chart === 'undefined') return;

        const dist = (d.pipeline_stalls || {}).issue_width_dist || {};
        this.charts.issueWidth = new Chart(ctx, {
            type: 'bar',
            data: {
                labels: [this.benchName],
                datasets: [
                    { label: '0 issued', data: [dist['0'] || 0], backgroundColor: this.PINK_BG, borderColor: this.PINK, borderWidth: 1 },
                    { label: '1 issued', data: [dist['1'] || 0], backgroundColor: this.ORANGE_BG, borderColor: this.ORANGE, borderWidth: 1 },
                    { label: '2 issued', data: [dist['2'] || 0], backgroundColor: this.CYAN_BG, borderColor: this.CYAN, borderWidth: 1 },
                    { label: '3 issued', data: [dist['3'] || 0], backgroundColor: this.GREEN_BG, borderColor: this.GREEN, borderWidth: 1 },
                    { label: '4+ issued', data: [dist['4_plus'] || 0], backgroundColor: this.BLUE_BG, borderColor: this.BLUE, borderWidth: 1 }
                ]
            },
            options: {
                responsive: true,
                scales: {
                    x: { grid: { display: false }, ticks: { color: this.TICK }, stacked: true },
                    y: { grid: { color: this.GRID }, ticks: { color: this.TICK }, stacked: true, title: { display: true, text: 'Cycles', color: this.TICK } }
                },
                plugins: this._commonPlugins()
            }
        });
    }

    // ===================== CPI / IPC OVERVIEW =====================
    _renderIPC(d) {
        const ctx = document.getElementById('chartIPC');
        if (!ctx || typeof Chart === 'undefined') return;

        this.charts.ipc = new Chart(ctx, {
            type: 'bar',
            data: {
                labels: [this.benchName],
                datasets: [
                    {
                        label: 'IPC',
                        data: [d.ipc || 0],
                        backgroundColor: this.BLUE_BG,
                        borderColor: this.BLUE,
                        borderWidth: 1,
                        yAxisID: 'y'
                    },
                    {
                        label: 'CPI',
                        data: [d.cpi || 0],
                        backgroundColor: this.ORANGE_BG,
                        borderColor: this.ORANGE,
                        borderWidth: 1,
                        yAxisID: 'y'
                    },
                    {
                        label: 'Total Instructions',
                        data: [d.total_instructions || 0],
                        type: 'line',
                        borderColor: this.GREEN,
                        backgroundColor: 'rgba(63,185,80,0.1)',
                        borderWidth: 2,
                        pointRadius: 4,
                        pointBackgroundColor: this.GREEN,
                        tension: 0.3,
                        yAxisID: 'y1'
                    }
                ]
            },
            options: {
                responsive: true,
                scales: {
                    x: { grid: { display: false }, ticks: { color: this.TICK } },
                    y: {
                        grid: { color: this.GRID },
                        ticks: { color: this.TICK },
                        position: 'left',
                        title: { display: true, text: 'IPC / CPI', color: this.TICK }
                    },
                    y1: {
                        grid: { display: false },
                        ticks: { color: this.GREEN },
                        position: 'right',
                        title: { display: true, text: 'Instructions', color: this.GREEN }
                    }
                },
                plugins: this._commonPlugins()
            }
        });
    }

    // ===================== DETAILED METRICS TABLE =====================
    _renderTable(d) {
        const tbody = this.elements.statsTableBody;
        if (!tbody) return;

        const bp = d.branch_predictor || {};
        const stalls = d.pipeline_stalls || {};
        const cache = d.cache_detail || {};
        const totalCycles = d.total_cycles || 1;

        const acc = bp.accuracy || 0;
        const stallPct = stalls.total_stall_cycles ? (stalls.total_stall_cycles / totalCycles * 100).toFixed(1) : '0.0';

        const rows = [
            ['Total Instructions', (d.total_instructions || 0).toLocaleString()],
            ['Total Cycles', (d.total_cycles || 0).toLocaleString()],
            ['IPC', (d.ipc || 0).toFixed(3)],
            ['CPI', (d.cpi || 0).toFixed(3)],
            ['Memory Instr %', (d.memory_instr_pct || 0).toFixed(1) + '%'],
            ['Branch Instr %', (d.branch_instr_pct || 0).toFixed(1) + '%'],
            ['Total Branches', (bp.total_branches || 0).toLocaleString()],
            ['Correct Predictions', (bp.correct || 0).toLocaleString()],
            ['Mispredictions', (bp.mispredictions || 0).toLocaleString()],
            ['Branch Accuracy', acc.toFixed(1) + '%'],
            ['BTB Hit Rate', (bp.btb_hit_rate || 0).toFixed(1) + '%'],
            ['RAS Hits / Misses', `${bp.ras_hits || 0} / ${bp.ras_misses || 0}`],
            ['Squashes', (bp.squashes || 0).toLocaleString()],
            ['Branch MPKI', (bp.branch_mpki || 0).toFixed(1)],
            ['ROB Full Stalls', (stalls.rob_full || 0).toLocaleString()],
            ['IQ Full Stalls', (stalls.iq_full || 0).toLocaleString()],
            ['LSQ Full Stalls', (stalls.lsq_full || 0).toLocaleString()],
            ['Cache Miss Stalls', (stalls.cache_miss || 0).toLocaleString()],
            ['Branch Mispred Stalls', (stalls.branch_mispredict || 0).toLocaleString()],
            ['Total Stall Cycles', `${(stalls.total_stall_cycles || 0).toLocaleString()} (${stallPct}%)`],
            ['L1 Reads / Writes', `${(cache.l1?.reads || 0)} / ${(cache.l1?.writes || 0)}`],
            ['L1 Read Misses', (cache.l1?.read_misses || 0).toLocaleString()],
            ['L1 Write Misses', (cache.l1?.write_misses || 0).toLocaleString()],
            ['L2 Reads / Writes', `${(cache.l2?.reads || 0)} / ${(cache.l2?.writes || 0)}`],
            ['Wall Time', (d.wall_time_ms || 0).toFixed(1) + ' ms'],
            ['Instructions/sec', (d.instr_per_sec || 0).toLocaleString()],
            ['Cycles/sec', (d.cycles_per_sec || 0).toLocaleString()]
        ];

        tbody.innerHTML = rows.map(([metric, value]) => {
            // Color-code certain values
            let cls = '';
            if (metric === 'Branch Accuracy') {
                const num = parseFloat(value);
                cls = num >= 70 ? 'good' : num >= 40 ? 'warn' : 'bad';
            } else if (metric === 'Total Stall Cycles') {
                const pct = parseFloat(stallPct);
                cls = pct > 60 ? 'bad' : pct > 20 ? 'warn' : 'good';
            }
            return `<tr><td>${metric}</td><td class="${cls}">${value}</td></tr>`;
        }).join('');
    }
}
