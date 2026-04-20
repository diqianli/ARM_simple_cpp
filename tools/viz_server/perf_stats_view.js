// Performance Statistics View — Time Series Charts for ARM CPU Emulator
// Renders 4 time series line charts (IPC, Cache Miss Rate, Branch Mispred Rate, Stall Rate)
// from perf stats JSON with interval-based sampling data

class PerfStatsView {
    constructor() {
        this.charts = {};
        this.data = null;
        this.benchName = '';

        // Color constants
        this.BLUE = '#58a6ff';
        this.PINK = '#f97583';
        this.GREEN = '#3fb950';
        this.ORANGE = '#d29922';
        this.PURPLE = '#bc8cff';
        this.CYAN = '#56d4dd';
        this.TICK = '#8b949e';
        this.GRID = 'rgba(139,148,158,0.15)';

        // DOM elements
        this.elements = {
            statsContent: document.getElementById('statsContent'),
            statsCards: document.getElementById('statsCards'),
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

        if (!data.time_series || !data.time_series.samples.length) {
            if (this.elements.statsCards) {
                this.elements.statsCards.innerHTML = '<p style="color:#8b949e;">No time series data available.</p>';
            }
            return;
        }

        const { samples } = data.time_series;

        this._renderCards(data, samples);

        // Compute overall averages from the data
        const avgIPC = data.ipc || 0;
        const avgCacheMiss = data.l1_hit_rate != null ? (1 - data.l1_hit_rate) : 0;
        const avgBranchMP = data.branch_predictor
            ? (1 - data.branch_predictor.accuracy / 100) : 0;
        const totalCycles = data.total_cycles || 1;
        const avgStall = data.pipeline_stalls
            ? (data.pipeline_stalls.total_stall_cycles / totalCycles) : 0;

        this._renderTimeSeriesChart('chart-ipc', 'IPC', samples, 'ipc', avgIPC, this.BLUE, '');
        this._renderTimeSeriesChart('chart-cache-miss', 'Cache Miss Rate', samples, 'cache_miss_rate', avgCacheMiss, this.PINK, '');
        this._renderTimeSeriesChart('chart-branch-mp', 'Branch Mispred Rate', samples, 'branch_mispred_rate', avgBranchMP, this.ORANGE, '');
        this._renderTimeSeriesChart('chart-stall', 'Pipeline Stall Rate', samples, 'stall_rate', avgStall, this.PURPLE, '');

        // Simulation speed chart (wall-time based)
        if (data.wall_time_series && data.wall_time_series.samples.length) {
            const wts = data.wall_time_series.samples;
            const labels = wts.map(s => s.wall_time_sec.toFixed(0) + 's');
            const values = wts.map(s => s.instr_per_sec);
            const avgSpeed = data.instr_per_sec || 0;
            this._renderWallTimeChart('chart-sim-speed', 'Instructions/sec', labels, values, avgSpeed, this.CYAN);
        }
    }

    // ===================== SUMMARY CARDS =====================
    _renderCards(d, samples) {
        const bp = d.branch_predictor || {};
        const stalls = d.pipeline_stalls || {};
        const totalCycles = d.total_cycles || 1;

        const ipc = d.ipc || 0;
        const mispredRate = bp.total_branches ? (bp.mispredictions / bp.total_branches * 100) : 0;
        const stallRate = stalls.total_stall_cycles ? (stalls.total_stall_cycles / totalCycles * 100) : 0;
        const cacheMissRate = d.l1_hit_rate != null ? ((1 - d.l1_hit_rate) * 100) : 0;

        if (this.elements.statsCards) {
            this.elements.statsCards.innerHTML = `
                <div class="stats-card c-ipc">
                    <div class="stats-card-label">IPC</div>
                    <div class="stats-card-value">${ipc.toFixed(3)}</div>
                    <div class="stats-card-sub">${(d.total_instructions || 0).toLocaleString()} instructions in ${(d.total_cycles || 0).toLocaleString()} cycles</div>
                </div>
                <div class="stats-card c-mispred">
                    <div class="stats-card-label">Branch Mispred Rate</div>
                    <div class="stats-card-value">${mispredRate.toFixed(1)}%</div>
                    <div class="stats-card-sub">${(bp.mispredictions || 0)} mispredictions out of ${(bp.total_branches || 0)} branches</div>
                </div>
                <div class="stats-card c-miss">
                    <div class="stats-card-label">Cache Miss Rate</div>
                    <div class="stats-card-value">${cacheMissRate.toFixed(1)}%</div>
                    <div class="stats-card-sub">L1 hit rate: ${((d.l1_hit_rate || 0) * 100).toFixed(1)}%</div>
                </div>
                <div class="stats-card c-stall">
                    <div class="stats-card-label">Stall Rate</div>
                    <div class="stats-card-value">${stallRate.toFixed(1)}%</div>
                    <div class="stats-card-sub">${(stalls.total_stall_cycles || 0)} stall cycles / ${totalCycles} total</div>
                </div>
                <div class="stats-card c-ipc">
                    <div class="stats-card-label">Total Instructions</div>
                    <div class="stats-card-value">${(d.total_instructions || 0).toLocaleString()}</div>
                    <div class="stats-card-sub">across ${(d.total_cycles || 0).toLocaleString()} cycles</div>
                </div>
                <div class="stats-card c-miss">
                    <div class="stats-card-label">Total Simulation Time</div>
                    <div class="stats-card-value">${((d.wall_time_ms || 0) / 1000).toFixed(2)}s</div>
                    <div class="stats-card-sub">${(d.instr_per_sec || 0).toLocaleString(undefined, {maximumFractionDigits: 0})} instr/s avg</div>
                </div>
            `;
        }
    }

    // ===================== TIME SERIES CHART =====================
    _renderTimeSeriesChart(canvasId, label, samples, valueKey, avgValue, color, unit) {
        const ctx = document.getElementById(canvasId);
        if (!ctx || typeof Chart === 'undefined') return;

        const labels = samples.map(s => (s.cycle_start / 1000).toFixed(1));
        const values = samples.map(s => s[valueKey]);

        // Average line (dashed)
        const avgLine = samples.map(() => avgValue);

        this.charts[canvasId] = new Chart(ctx, {
            type: 'line',
            data: {
                labels: labels,
                datasets: [
                    {
                        label: label + ' (per interval)',
                        data: values,
                        borderColor: color,
                        backgroundColor: color.replace(')', ', 0.1)').replace('rgb', 'rgba'),
                        borderWidth: 2,
                        pointRadius: samples.length > 100 ? 0 : 2,
                        pointHoverRadius: 4,
                        tension: 0.2,
                        fill: false
                    },
                    {
                        label: label + ' (average)',
                        data: avgLine,
                        borderColor: 'rgba(139,148,158,0.6)',
                        borderWidth: 1.5,
                        borderDash: [8, 4],
                        pointRadius: 0,
                        pointHoverRadius: 0,
                        fill: false
                    }
                ]
            },
            options: {
                responsive: true,
                interaction: {
                    mode: 'index',
                    intersect: false
                },
                scales: {
                    x: {
                        grid: { color: this.GRID },
                        ticks: { color: this.TICK, maxTicksLimit: 20 },
                        title: { display: true, text: 'Cycle (K)', color: this.TICK }
                    },
                    y: {
                        grid: { color: this.GRID },
                        ticks: { color: this.TICK },
                        title: { display: true, text: label, color: this.TICK },
                        beginAtZero: true
                    }
                },
                plugins: this._commonPlugins()
            }
        });
    }

    // ===================== WALL TIME CHART =====================
    _renderWallTimeChart(canvasId, label, labels, values, avgValue, color) {
        const ctx = document.getElementById(canvasId);
        if (!ctx || typeof Chart === 'undefined') return;

        const avgLine = labels.map(() => avgValue);

        this.charts[canvasId] = new Chart(ctx, {
            type: 'bar',
            data: {
                labels: labels,
                datasets: [
                    {
                        label: label + ' (per 1s interval)',
                        data: values,
                        backgroundColor: color + '99',
                        borderColor: color,
                        borderWidth: 1,
                        borderRadius: 3
                    },
                    {
                        label: label + ' (average)',
                        data: avgLine,
                        type: 'line',
                        borderColor: 'rgba(139,148,158,0.6)',
                        borderWidth: 1.5,
                        borderDash: [8, 4],
                        pointRadius: 0,
                        pointHoverRadius: 0,
                        fill: false
                    }
                ]
            },
            options: {
                responsive: true,
                interaction: {
                    mode: 'index',
                    intersect: false
                },
                scales: {
                    x: {
                        grid: { color: this.GRID },
                        ticks: { color: this.TICK },
                        title: { display: true, text: 'Wall Time (s)', color: this.TICK }
                    },
                    y: {
                        grid: { color: this.GRID },
                        ticks: { color: this.TICK },
                        title: { display: true, text: label, color: this.TICK },
                        beginAtZero: true
                    }
                },
                plugins: this._commonPlugins()
            }
        });
    }
}
