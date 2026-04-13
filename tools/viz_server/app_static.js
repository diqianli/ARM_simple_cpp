// ARM CPU Emulator Static Visualization Client
// Loads Konata JSON data from file instead of WebSocket
// Standalone version — no server required

class StaticVisualization {
    constructor() {
        this.pipelineView = null;
        this.data = null;

        // Default data file to try loading
        this.defaultDataFile = 'viz_demo_data.json';

        // DOM elements
        this.elements = {
            totalCycles: document.getElementById('totalCycles'),
            totalInstructions: document.getElementById('totalInstructions'),
            opsCount: document.getElementById('opsCount'),
            fileInput: document.getElementById('fileInput'),
            reloadBtn: document.getElementById('reloadBtn'),
            statusBar: document.getElementById('statusBar'),
            pipelineViewContainer: document.getElementById('pipelineViewContainer'),
            dropZone: document.getElementById('dropZone')
        };

        this.init();
    }

    init() {
        // Initialize event listeners
        this.elements.fileInput.addEventListener('change', (e) => this.handleFileSelect(e));
        this.elements.reloadBtn.addEventListener('click', () => this.loadDefaultFile());

        // Drag and drop support
        this.initDragDrop();

        // Initialize pipeline view
        this.initPipelineView();

        // Try to load default file
        this.loadDefaultFile();
    }

    initDragDrop() {
        const dropZone = this.elements.dropZone;

        // Show drop zone on drag over body
        document.body.addEventListener('dragenter', (e) => {
            e.preventDefault();
            if (dropZone) dropZone.style.display = 'block';
        });

        document.body.addEventListener('dragover', (e) => {
            e.preventDefault();
            if (dropZone) dropZone.classList.add('drag-over');
        });

        document.body.addEventListener('dragleave', (e) => {
            // Only hide if leaving the body
            if (e.relatedTarget === null) {
                if (dropZone) {
                    dropZone.style.display = 'none';
                    dropZone.classList.remove('drag-over');
                }
            }
        });

        document.body.addEventListener('drop', (e) => {
            e.preventDefault();
            if (dropZone) {
                dropZone.style.display = 'none';
                dropZone.classList.remove('drag-over');
            }

            const file = e.dataTransfer.files[0];
            if (file && file.name.endsWith('.json')) {
                this.loadFile(file);
            } else {
                this.updateStatus('Please drop a .json file');
            }
        });
    }

    initPipelineView() {
        if (typeof PipelineView === 'undefined') {
            console.error('PipelineView not loaded');
            this.elements.pipelineViewContainer.innerHTML =
                '<div style="color: #e94560; padding: 20px; text-align: center;">PipelineView not loaded. Check console for errors.</div>';
            return;
        }

        this.pipelineView = new PipelineView('pipelineViewContainer', {
            autoConnect: false  // Don't connect to WebSocket
        });

        console.log('PipelineView initialized');
    }

    async loadDefaultFile() {
        this.updateStatus(`Loading ${this.defaultDataFile}...`);

        try {
            const response = await fetch(this.defaultDataFile);
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}: ${response.statusText}`);
            }

            const data = await response.json();
            this.handleData(data);
            this.updateStatus(`Loaded ${this.defaultDataFile} successfully`);
        } catch (error) {
            console.warn('Could not load default file:', error.message);
            // On file:// protocol, fetch fails due to CORS.
            // Fall back to inline demo data.
            console.log('Using inline demo data fallback');
            const demoData = this.getInlineDemoData();
            this.handleData(demoData);
            this.updateStatus('Loaded built-in demo data (12 instructions). Use "Load JSON File" to load custom data.');
        }
    }

    /**
     * Inline demo data — same pipeline trace as viz_demo.cpp
     * Used when fetch fails (e.g. file:// protocol CORS restrictions).
     */
    getInlineDemoData() {
        const S = (name, s, e) => ({ name, start_cycle: s, end_cycle: e });
        const mk = (id, gid, pc, label, stages, srcR, dstR, prods, isMem, memAddr) => ({
            id, gid, rid: 0, pc, label_name: label,
            lanes: { main: { name: 'main', stages } },
            prods: prods || [],
            src_regs: srcR || [], dst_regs: dstR || [],
            is_memory: !!isMem, mem_addr: memAddr || null,
            fetched_cycle: stages[0].start_cycle,
            retired_cycle: stages[stages.length - 1].end_cycle
        });

        return {
            version: '1.0',
            total_cycles: 100,
            total_instructions: 12,
            ops_count: 12,
            ops: [
                mk(0, 0, 0x1000, 'ADD X0, X1, X2',
                    [S('IF',0,1),S('DE',1,2),S('RN',2,3),S('DI',3,4),S('IS',4,5),S('EX',5,7),S('WB',7,8),S('RR',8,9)],
                    [1,2],[0],[]),
                mk(1, 1, 0x1004, 'MUL X3, X4, X5',
                    [S('IF',0,1),S('DE',1,2),S('RN',2,3),S('DI',3,4),S('IS',9,10),S('EX',10,13),S('WB',13,14),S('RR',14,15)],
                    [4,5],[3],[{producer_id:0,dep_type:'Register'}]),
                mk(2, 2, 0x1008, 'LDR X6, [X7]',
                    [S('IF',1,2),S('DE',2,3),S('RN',3,4),S('DI',4,5),S('IS',5,6),S('ME',6,7),S('ME:L1',6,7),S('WB',7,8),S('RR',11,12)],
                    [7],[6],[],true,0x80000),
                mk(3, 3, 0x100C, 'ADD X8, X6, X9',
                    [S('IF',1,2),S('DE',2,3),S('RN',3,4),S('DI',4,5),S('IS',12,13),S('EX',13,14),S('WB',14,15),S('RR',15,16)],
                    [6,9],[8],[{producer_id:2,dep_type:'Register'}]),
                mk(4, 4, 0x1010, 'LDR X10, [X11]',
                    [S('IF',2,3),S('DE',3,4),S('RN',4,5),S('DI',5,6),S('IS',6,7),S('ME',7,17),S('ME:L1',7,8),S('ME:L2',8,12),S('ME:L1-fill',12,13),S('WB',17,18),S('RR',21,22)],
                    [11],[10],[],true,0x100000),
                mk(5, 5, 0x1014, 'LDR X12, [X13]',
                    [S('IF',3,4),S('DE',4,5),S('RN',5,6),S('DI',6,7),S('IS',7,8),S('ME',8,27),S('ME:L1',8,9),S('ME:L2',9,13),S('ME:L3',13,19),S('ME:L2-fill',19,20),S('ME:L1-fill',20,21),S('WB',27,28),S('RR',31,32)],
                    [13],[12],[],true,0x800000),
                mk(6, 6, 0x1018, 'STR X14, [X15]',
                    [S('IF',4,5),S('DE',5,6),S('RN',6,7),S('DI',7,8),S('IS',8,9),S('ME',9,10),S('ME:L1',9,10),S('WB',10,11),S('RR',15,16)],
                    [14,15],[],[],true,0x80020),
                mk(7, 7, 0x101C, 'LDR X16, [X17]',
                    [S('IF',5,6),S('DE',6,7),S('RN',7,8),S('DI',8,9),S('IS',9,10),S('ME',10,46),S('ME:L1',10,11),S('ME:L2',11,15),S('ME:L3',15,21),S('ME:Memory',21,40),S('ME:L3-fill',40,41),S('ME:L2-fill',41,42),S('ME:L1-fill',42,43),S('WB',46,47),S('RR',51,52)],
                    [17],[16],[],true,0x4000000),
                mk(8, 8, 0x1020, 'FADD V0.4S, V1.4S, V2.4S',
                    [S('IF',6,7),S('DE',7,8),S('RN',8,9),S('DI',9,10),S('IS',10,11),S('EX',11,13),S('WB',13,14),S('RR',17,18)],
                    [1,2],[0],[]),
                mk(9, 9, 0x1024, 'FDIV V3.4S, V4.4S, V5.4S',
                    [S('IF',7,8),S('DE',8,9),S('RN',9,10),S('DI',10,11),S('IS',11,12),S('EX',12,20),S('WB',20,21),S('RR',27,28)],
                    [4,5],[3],[]),
                mk(10, 10, 0x1028, 'B #0x2000',
                    [S('IF',8,9),S('DE',9,10),S('RN',10,11),S('DI',11,12),S('IS',12,12),S('EX',12,12),S('WB',12,13),S('RR',13,14)],
                    [],[],[]),
                mk(11, 11, 0x102C, 'ADD X18, X0, X6',
                    [S('IF',2,3),S('DE',3,4),S('RN',4,5),S('DI',5,6),S('IS',16,17),S('EX',17,18),S('WB',18,19),S('RR',23,24)],
                    [0,6],[18],[{producer_id:0,dep_type:'Register'},{producer_id:2,dep_type:'Register'}])
            ]
        };
    }

    handleFileSelect(event) {
        const file = event.target.files[0];
        if (!file) return;

        this.loadFile(file);
    }

    loadFile(file) {
        this.updateStatus(`Loading ${file.name}...`);

        const reader = new FileReader();
        reader.onload = (e) => {
            try {
                const data = JSON.parse(e.target.result);
                this.handleData(data);
                this.updateStatus(`Loaded ${file.name} successfully`);
            } catch (error) {
                console.error('Failed to parse JSON:', error);
                this.updateStatus(`Error: Failed to parse JSON - ${error.message}`);
            }
        };
        reader.onerror = () => {
            this.updateStatus('Error reading file');
        };
        reader.readAsText(file);
    }

    handleData(data) {
        console.log('Received data:', data);
        this.data = data;

        // Update metrics — support both KonataExport and KonataSnapshot formats
        const totalCycles = data.total_cycles || data.cycle || 0;
        const totalInstr = data.total_instructions || data.committed_count || 0;
        const opsCount = data.ops_count || (data.ops ? data.ops.length : 0);

        this.elements.totalCycles.textContent = totalCycles.toLocaleString();
        this.elements.totalInstructions.textContent = totalInstr.toLocaleString();
        this.elements.opsCount.textContent = opsCount.toLocaleString();

        // Convert to Konata snapshot format if needed
        const snapshot = this.normalizeData(data);

        // Update pipeline view
        if (this.pipelineView && snapshot.ops && snapshot.ops.length > 0) {
            console.log('Updating pipeline view with', snapshot.ops.length, 'ops');
            this.pipelineView.updateSnapshot(snapshot);
        } else {
            console.warn('Pipeline view not ready or no ops in data');
        }
    }

    normalizeData(data) {
        // KonataExport format: { version, total_cycles, total_instructions, ops_count, ops }
        if (data.ops && Array.isArray(data.ops)) {
            return {
                cycle: data.total_cycles || data.cycle || 0,
                committed_count: data.total_instructions || data.committed_count || 0,
                ops: data.ops,
                metadata: data.metadata || {}
            };
        }

        // If data is an array of snapshots, merge them
        if (Array.isArray(data)) {
            const allOps = new Map();

            for (const snapshot of data) {
                if (snapshot.ops) {
                    for (const op of snapshot.ops) {
                        if (!allOps.has(op.id)) {
                            allOps.set(op.id, op);
                        }
                    }
                }
            }

            const sortedOps = Array.from(allOps.values()).sort((a, b) => a.id - b.id);

            return {
                cycle: data[data.length - 1]?.cycle || 0,
                committed_count: data[data.length - 1]?.committed_count || 0,
                ops: sortedOps,
                metadata: {}
            };
        }

        // Return as-is if we can't normalize
        return data;
    }

    updateStatus(message) {
        if (this.elements.statusBar) {
            this.elements.statusBar.textContent = message;
        }
        console.log('Status:', message);
    }
}

// Initialize when DOM is loaded
document.addEventListener('DOMContentLoaded', () => {
    window.staticViz = new StaticVisualization();
});
