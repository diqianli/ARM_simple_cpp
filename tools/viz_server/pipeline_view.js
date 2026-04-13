/**
 * Pipeline View Component (Standalone Version)
 *
 * Manages the Konata renderer instance, handles file loading/export,
 * and provides the UI controls for the pipeline visualization.
 * WebSocket/API code removed — file-only operation.
 */

class PipelineView {
    constructor(containerId, options = {}) {
        this.containerId = containerId;
        this.options = {
            autoConnect: false,
            showControls: true,
            ...options
        };

        this.container = document.getElementById(containerId);
        if (!this.container) {
            console.error(`Container #${containerId} not found`);
            return;
        }

        this.renderer = null;
        this.currentSnapshot = null;

        this.init();
    }

    /**
     * Initialize the pipeline view
     */
    init() {
        // Create container structure
        this.container.innerHTML = `
            <div class="pipeline-view">
                <div class="pipeline-controls">
                    <div class="control-group">
                        <button id="pv-zoom-in" class="btn btn-sm" title="Zoom In">+</button>
                        <button id="pv-zoom-out" class="btn btn-sm" title="Zoom Out">-</button>
                        <button id="pv-reset-view" class="btn btn-sm" title="Reset View">&#8634;</button>
                    </div>
                    <div class="control-group">
                        <span class="control-label">Label:</span>
                        <button id="pv-label-widen" class="btn btn-sm" title="Widen Labels">&#9664;</button>
                        <button id="pv-label-narrow" class="btn btn-sm" title="Narrow Labels">&#9654;</button>
                    </div>
                    <div class="control-group">
                        <input type="text" id="pv-search" class="search-input" placeholder="Search instructions...">
                        <button id="pv-search-prev" class="btn btn-sm" title="Previous">&#9650;</button>
                        <button id="pv-search-next" class="btn btn-sm" title="Next">&#9660;</button>
                        <span id="pv-search-count" class="search-count"></span>
                    </div>
                    <div class="control-group">
                        <button id="pv-export" class="btn btn-sm" title="Export Data">&#8681; Export</button>
                        <label id="pv-import-label" class="btn btn-sm" title="Import Data">
                            &#8679; Import
                            <input type="file" id="pv-import" accept=".json" style="display: none;">
                        </label>
                    </div>
                </div>
                <div id="pv-canvas-container" class="canvas-container"></div>
                <div id="pv-info-panel" class="info-panel">
                    <div class="info-row">
                        <span class="info-label">Selected:</span>
                        <span id="pv-selected-info" class="info-value">None</span>
                    </div>
                </div>
            </div>
        `;

        // Get elements
        this.canvasContainer = document.getElementById('pv-canvas-container');
        this.searchInput = document.getElementById('pv-search');
        this.searchCount = document.getElementById('pv-search-count');
        this.selectedInfo = document.getElementById('pv-selected-info');

        // Initialize renderer
        this.initRenderer();

        // Bind events
        this.bindEvents();
    }

    /**
     * Initialize the Konata renderer
     */
    initRenderer() {
        // Check if KonataRenderer is available
        if (typeof KonataRenderer === 'undefined') {
            console.error('KonataRenderer not loaded. Make sure konata_renderer.js is included.');
            this.canvasContainer.innerHTML = '<div class="error-message" style="color: #e94560; padding: 20px; text-align: center;">Renderer not loaded. Check console for errors.</div>';
            return;
        }

        console.log('PipelineView: Initializing KonataRenderer');
        this.renderer = new KonataRenderer(this.canvasContainer);

        // Listen for selection events
        this.renderer.on('select', (e) => {
            this.onOpSelected(e.detail);
        });

        console.log('PipelineView: Renderer initialized successfully');
    }

    /**
     * Bind UI events
     */
    bindEvents() {
        // Zoom controls
        document.getElementById('pv-zoom-in').addEventListener('click', () => {
            if (this.renderer) this.renderer.zoomIn();
        });

        document.getElementById('pv-zoom-out').addEventListener('click', () => {
            if (this.renderer) this.renderer.zoomOut();
        });

        document.getElementById('pv-reset-view').addEventListener('click', () => {
            if (this.renderer) this.renderer.resetView();
        });

        // Label width controls
        document.getElementById('pv-label-widen').addEventListener('click', () => {
            if (this.renderer && this.renderer.increaseLabelWidth) this.renderer.increaseLabelWidth();
        });

        document.getElementById('pv-label-narrow').addEventListener('click', () => {
            if (this.renderer && this.renderer.decreaseLabelWidth) this.renderer.decreaseLabelWidth();
        });

        // Search
        this.searchInput.addEventListener('input', (e) => {
            this.handleSearch(e.target.value);
        });

        this.searchInput.addEventListener('keydown', (e) => {
            if (e.key === 'Enter') {
                if (e.shiftKey) {
                    if (this.renderer) this.renderer.prevSearchResult();
                } else {
                    if (this.renderer) this.renderer.nextSearchResult();
                }
            }
        });

        document.getElementById('pv-search-prev').addEventListener('click', () => {
            if (this.renderer) this.renderer.prevSearchResult();
        });

        document.getElementById('pv-search-next').addEventListener('click', () => {
            if (this.renderer) this.renderer.nextSearchResult();
        });

        // Export
        document.getElementById('pv-export').addEventListener('click', () => {
            this.exportData();
        });

        // Import
        document.getElementById('pv-import').addEventListener('change', (e) => {
            this.importData(e.target.files[0]);
        });

        // Keyboard shortcuts
        document.addEventListener('keydown', (e) => {
            if (e.target === this.searchInput) return;

            if (e.ctrlKey || e.metaKey) {
                switch (e.key) {
                    case 'f':
                        e.preventDefault();
                        this.searchInput.focus();
                        break;
                    case 'e':
                        e.preventDefault();
                        this.exportData();
                        break;
                }
            }
        });
    }

    /**
     * Handle search input
     */
    handleSearch(query) {
        if (!this.renderer) return;

        const count = this.renderer.search(query);
        this.searchCount.textContent = count > 0 ? `${count} found` : '';
    }

    /**
     * Handle operation selection
     */
    onOpSelected(op) {
        if (!op) {
            this.selectedInfo.textContent = 'None';
            return;
        }

        const regs = op.formatRegs();
        const mem = op.formatMemAddr();

        let info = `[${op.id}] ${op.formatPC()} ${op.labelName}`;
        if (mem) {
            info += ` | Mem: ${mem}`;
        }

        this.selectedInfo.textContent = info;
    }

    /**
     * Export data to JSON file
     */
    exportData() {
        if (!this.renderer) return;

        const data = this.renderer.exportData();
        const json = JSON.stringify(data, null, 2);
        const blob = new Blob([json], { type: 'application/json' });
        const url = URL.createObjectURL(blob);

        const a = document.createElement('a');
        a.href = url;
        a.download = `konata-export-${Date.now()}.json`;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
    }

    /**
     * Import data from JSON file
     */
    async importData(file) {
        if (!file || !this.renderer) return;

        try {
            const text = await file.text();
            const data = JSON.parse(text);

            // Handle both single snapshot and export format
            const snapshot = data.snapshots ? data.snapshots[data.snapshots.length - 1] : data;

            if (snapshot.ops) {
                this.currentSnapshot = snapshot;
                this.renderer.updateSnapshot(snapshot);
            }
        } catch (error) {
            console.error('PipelineView: Failed to import data', error);
            alert('Failed to import data: ' + error.message);
        }
    }

    /**
     * Set operations directly
     */
    setOps(ops) {
        if (this.renderer) {
            this.renderer.setOps(ops);
        }
    }

    /**
     * Update with a new snapshot
     */
    updateSnapshot(snapshot) {
        console.log('PipelineView: Updating snapshot', snapshot ? `with ${snapshot.ops ? snapshot.ops.length : 0} ops` : 'null');
        this.currentSnapshot = snapshot;
        if (this.renderer) {
            this.renderer.updateSnapshot(snapshot);
        } else {
            console.warn('PipelineView: Renderer not available');
        }
    }

    /**
     * Get current snapshot
     */
    getSnapshot() {
        return this.currentSnapshot;
    }

    /**
     * Clear the visualization
     */
    clear() {
        this.currentSnapshot = null;
        if (this.renderer) {
            this.renderer.setOps([]);
        }
    }

    /**
     * Destroy the pipeline view
     */
    destroy() {
        if (this.renderer) {
            this.renderer.destroy();
        }
        this.container.innerHTML = '';
    }
}

// Expose globally for browser usage
if (typeof window !== 'undefined') {
    window.PipelineView = PipelineView;
}
