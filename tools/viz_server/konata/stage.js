/**
 * Konata Stage Renderer
 *
 * Handles rendering of individual pipeline stages.
 */

/**
 * Stage rendering configuration
 */
const STAGE_CONFIG = {
    minHeight: 2,
    defaultHeight: 20,
    borderRadius: 3,
    borderWidth: 1,
    fontSize: 10,
    labelPadding: 4
};

/**
 * Render a single stage on a canvas context
 *
 * @param {CanvasRenderingContext2D} ctx - Canvas context
 * @param {Stage} stage - Stage object to render
 * @param {Object} layout - Layout information {x, y, width, height, cycleToX, cycleWidth}
 * @param {Object} options - Rendering options
 */
function renderStage(ctx, stage, layout, options = {}) {
    const {
        x: baseX,
        y: baseY,
        cycleToX,
        cycleWidth,
        height,
        showLabels = true,
        highlighted = false,
        alpha = 1.0
    } = layout;

    const startX = cycleToX(stage.startCycle);
    const endX = cycleToX(stage.endCycle);
    const width = Math.max(endX - startX, cycleWidth);
    const numCycles = stage.endCycle - stage.startCycle;

    // Draw stage rectangle
    ctx.save();

    // Fill
    const baseColor = stage.cssColor;
    ctx.fillStyle = highlighted
        ? stage.cssColorTransparent(0.9 * alpha)
        : stage.cssColorTransparent(0.7 * alpha);
    ctx.strokeStyle = baseColor;
    ctx.lineWidth = highlighted ? 2 : 1;

    // Draw rounded rectangle
    const radius = Math.min(STAGE_CONFIG.borderRadius, height / 2, width / 2);
    roundRect(ctx, startX, baseY, width, height, radius);
    ctx.fill();
    ctx.stroke();

    // Draw cycle separator lines within stage
    if (numCycles > 1 && cycleWidth > 4) {
        ctx.strokeStyle = 'rgba(255, 255, 255, 0.5)';
        ctx.lineWidth = 2;

        for (let c = 1; c < numCycles; c++) {
            const cycleX = cycleToX(stage.startCycle + c);
            ctx.beginPath();
            ctx.moveTo(cycleX, baseY);
            ctx.lineTo(cycleX, baseY + height);
            ctx.stroke();
        }
    }

    // Draw content in each cycle cell
    if (numCycles > 0 && cycleWidth > 6) {
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';

        for (let c = 0; c < numCycles; c++) {
            const cycleX = cycleToX(stage.startCycle + c);
            const cycleEndX = cycleToX(stage.startCycle + c + 1);
            const cycleCenterX = (cycleX + cycleEndX) / 2;
            const cycleWidthPx = cycleEndX - cycleX;

            if (cycleWidthPx < 8) continue;

            // First cycle: show stage name
            // Last cycle: show stage name
            // Middle cycles: show cycle number
            const isFirst = (c === 0);
            const isLast = (c === numCycles - 1);

            if (isFirst || isLast) {
                // Show stage name (e.g., "F", "Dc", "Ex")
                ctx.fillStyle = '#ffffff';
                ctx.font = 'bold 10px monospace';
                ctx.fillText(stage.name, cycleCenterX, baseY + height / 2);
            } else {
                // Show cycle number (relative to stage start)
                ctx.fillStyle = 'rgba(255, 255, 255, 0.8)';
                ctx.font = '9px monospace';
                ctx.fillText(c.toString(), cycleCenterX, baseY + height / 2);
            }
        }
    }

    ctx.restore();

    return {
        x: startX,
        y: baseY,
        width: width,
        height: height
    };
}

/**
 * Render all stages for an instruction
 *
 * @param {CanvasRenderingContext2D} ctx - Canvas context
 * @param {Op} op - Operation/instruction object
 * @param {Object} layout - Layout information
 * @param {Object} options - Rendering options
 */
function renderOpStages(ctx, op, layout, options = {}) {
    const {
        y: baseY,
        cycleToX,
        cycleWidth,
        rowHeight,
        showLabels = true,
        highlighted = false,
        alpha = 1.0
    } = layout;

    const stageHeight = rowHeight - 4;
    const stageY = baseY + 2;

    const renderedStages = [];

    // Render stages from main lane
    const mainLane = op.lanes.get('main');
    if (mainLane) {
        for (const stage of mainLane.stages) {
            const bounds = renderStage(ctx, stage, {
                x: 0,
                y: stageY,
                cycleToX,
                cycleWidth,
                height: stageHeight,
                showLabels,
                highlighted,
                alpha
            }, options);

            renderedStages.push({
                stage,
                bounds
            });
        }
    }

    return renderedStages;
}

/**
 * Draw a rounded rectangle
 */
function roundRect(ctx, x, y, width, height, radius) {
    radius = Math.max(0, Math.min(radius, width / 2, height / 2));

    ctx.beginPath();
    ctx.moveTo(x + radius, y);
    ctx.lineTo(x + width - radius, y);
    ctx.quadraticCurveTo(x + width, y, x + width, y + radius);
    ctx.lineTo(x + width, y + height - radius);
    ctx.quadraticCurveTo(x + width, y + height, x + width - radius, y + height);
    ctx.lineTo(x + radius, y + height);
    ctx.quadraticCurveTo(x, y + height, x, y + height - radius);
    ctx.lineTo(x, y + radius);
    ctx.quadraticCurveTo(x, y, x + radius, y);
    ctx.closePath();
}

/**
 * Calculate stage layout bounds
 */
function calculateStageBounds(stages, cycleToX, cycleWidth, rowHeight) {
    const bounds = [];
    const stageHeight = rowHeight - 4;
    const stageY = 2;

    for (const stage of stages) {
        const startX = cycleToX(stage.startCycle);
        const endX = cycleToX(stage.endCycle);
        const width = Math.max(endX - startX, cycleWidth);

        bounds.push({
            stage,
            x: startX,
            y: stageY,
            width,
            height: stageHeight
        });
    }

    return bounds;
}

// Export for module usage
if (typeof module !== 'undefined' && module.exports) {
    module.exports = {
        renderStage,
        renderOpStages,
        roundRect,
        calculateStageBounds,
        STAGE_CONFIG
    };
}
// Expose globally for browser usage
if (typeof window !== 'undefined') {
    window.renderStage = renderStage;
    window.renderOpStages = renderOpStages;
    window.roundRect = roundRect;
    window.calculateStageBounds = calculateStageBounds;
    window.STAGE_CONFIG = STAGE_CONFIG;
}
