/**
 * Konata Operation (Instruction) Data Structure
 *
 * Represents a single instruction in the pipeline visualization.
 */

/**
 * Stage colors in HSL format
 * Uses standard Konata naming convention
 */
const STAGE_COLORS = {
    'IF': { h: 200, s: 70, l: 60, name: 'Instruction Fetch' },
    'DE': { h: 180, s: 60, l: 55, name: 'Decode' },
    'RN': { h: 160, s: 50, l: 50, name: 'Rename' },
    'DI': { h: 140, s: 60, l: 55, name: 'Dispatch' },
    'IS': { h: 120, s: 70, l: 50, name: 'Issue' },
    'EX': { h: 60,  s: 80, l: 55, name: 'Execute' },
    'ME': { h: 30,  s: 80, l: 55, name: 'Memory' },
    // Cache hierarchy sub-stages (ME:L1, ME:L2, ME:L3, ME:MEM)
    'ME:L1': { h: 30, s: 80, l: 55, name: 'L1 Cache' },
    'ME:L2': { h: 35, s: 75, l: 50, name: 'L2 Cache' },
    'ME:L3': { h: 40, s: 70, l: 45, name: 'L3 Cache' },
    'ME:MEM': { h: 0, s: 80, l: 40, name: 'DDR Memory' },
    'WB': { h: 280, s: 60, l: 55, name: 'Writeback' },
    'RR': { h: 320, s: 50, l: 50, name: 'Retire' }
};

/**
 * Dependency colors
 */
const DEP_COLORS = {
    'register': '#ff6600',  // Orange for register dependencies
    'memory': '#0066ff'     // Blue for memory dependencies
};

/**
 * Represents a single pipeline stage
 */
class Stage {
    constructor(name, startCycle, endCycle) {
        this.name = name;
        this.startCycle = startCycle;
        this.endCycle = endCycle;

        // Handle DI with waiting period (e.g., "DI:5-210")
        // Use the same color as the base stage
        let colorKey = name;
        if (name.startsWith('DI:')) {
            colorKey = 'DI';  // Use DI's color for DI:5-210 stages
        }
        this.color = STAGE_COLORS[colorKey] || { h: 0, s: 0, l: 50, name: name };
    }

    get duration() {
        return this.endCycle - this.startCycle;
    }

    get cssColor() {
        return `hsl(${this.color.h}, ${this.color.s}%, ${this.color.l}%)`;
    }

    cssColorTransparent(alpha = 0.3) {
        return `hsla(${this.color.h}, ${this.color.s}%, ${this.color.l}%, ${alpha})`;
    }
}

/**
 * Represents a dependency on another instruction
 */
class DependencyRef {
    constructor(producerId, depType) {
        this.producerId = producerId;
        this.depType = depType;
        this.color = DEP_COLORS[depType] || '#888888';
    }
}

/**
 * Represents a lane (execution unit) for an instruction
 */
class Lane {
    constructor(name) {
        this.name = name;
        this.stages = [];
    }

    addStage(stage) {
        this.stages.push(stage);
    }

    get earliestCycle() {
        if (this.stages.length === 0) return Infinity;
        return Math.min(...this.stages.map(s => s.startCycle));
    }

    get latestCycle() {
        if (this.stages.length === 0) return 0;
        return Math.max(...this.stages.map(s => s.endCycle));
    }
}

/**
 * Represents a complete instruction/operation in the pipeline
 */
class Op {
    constructor(data) {
        this.id = data.id;
        this.gid = data.gid;
        this.rid = data.rid;
        this.fetchedCycle = data.fetched_cycle;
        this.retiredCycle = data.retired_cycle;
        this.labelName = data.label_name;
        this.pc = data.pc;
        this.srcRegs = data.src_regs || [];
        this.dstRegs = data.dst_regs || [];
        this.isMemory = data.is_memory;
        this.memAddr = data.mem_addr;

        // Parse lanes
        this.lanes = new Map();
        if (data.lanes) {
            for (const [laneName, laneData] of Object.entries(data.lanes)) {
                const lane = new Lane(laneName);
                if (laneData.stages) {
                    for (const stageData of laneData.stages) {
                        lane.addStage(new Stage(
                            stageData.name,
                            stageData.start_cycle,
                            stageData.end_cycle
                        ));
                    }
                }
                this.lanes.set(laneName, lane);
            }
        }

        // Parse dependencies
        this.prods = [];
        if (data.prods) {
            for (const depData of data.prods) {
                this.prods.push(new DependencyRef(
                    depData.producer_id,
                    depData.dep_type
                ));
            }
        }

        // UI state
        this.x = 0;
        this.y = 0;
        this.width = 0;
        this.height = 0;
        this.visible = true;
        this.highlighted = false;
    }

    get earliestCycle() {
        let min = this.fetchedCycle;
        for (const lane of this.lanes.values()) {
            min = Math.min(min, lane.earliestCycle);
        }
        return min;
    }

    get latestCycle() {
        let max = this.retiredCycle || 0;
        for (const lane of this.lanes.values()) {
            max = Math.max(max, lane.latestCycle);
        }
        return max;
    }

    get totalLatency() {
        if (this.retiredCycle !== null && this.retiredCycle !== undefined) {
            return this.retiredCycle - this.fetchedCycle;
        }
        return null;
    }

    getAllStages() {
        const stages = [];
        for (const lane of this.lanes.values()) {
            stages.push(...lane.stages);
        }
        return stages.sort((a, b) => a.startCycle - b.startCycle);
    }

    getStageAt(cycle) {
        for (const lane of this.lanes.values()) {
            for (const stage of lane.stages) {
                if (cycle >= stage.startCycle && cycle < stage.endCycle) {
                    return stage;
                }
            }
        }
        return null;
    }

    formatRegs() {
        const src = this.srcRegs.map(r => `X${r}`).join(', ');
        const dst = this.dstRegs.map(r => `X${r}`).join(', ');
        return { src: src || '-', dst: dst || '-' };
    }

    formatPC() {
        return '0x' + this.pc.toString(16).padStart(8, '0');
    }

    formatMemAddr() {
        if (!this.isMemory || !this.memAddr) return null;
        return '0x' + this.memAddr.toString(16).padStart(8, '0');
    }
}

// Export for module usage
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { Op, Stage, Lane, DependencyRef, STAGE_COLORS, DEP_COLORS };
}
// Expose globally for browser usage
if (typeof window !== 'undefined') {
    window.Op = Op;
    window.Stage = Stage;
    window.Lane = Lane;
    window.DependencyRef = DependencyRef;
    window.STAGE_COLORS = STAGE_COLORS;
    window.DEP_COLORS = DEP_COLORS;
}
