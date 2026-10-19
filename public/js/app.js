// BuildMesh Frontend Controller
document.addEventListener('DOMContentLoaded', () => {
  // Application State Cache
  let currentModules = [];
  let currentGraph = { modules: [], validation: {} };
  let selectedModuleId = 'core_types';
  let lastPlan = null;
  let latestResult = null;

  // DOM Elements
  const serverStatusPill = document.getElementById('server-status');
  const serverStatusText = document.getElementById('server-status-text');
  const projectRevBadge = document.getElementById('project-rev-badge');
  const lastBuildStatusBadge = document.getElementById('last-build-status-badge');

  // Metrics
  const metricTotal = document.getElementById('metric-total-modules');
  const metricModified = document.getElementById('metric-modified-modules');
  const metricAffected = document.getElementById('metric-affected-modules');
  const metricCached = document.getElementById('metric-cached-artifacts');
  const metricDuration = document.getElementById('metric-last-duration');

  // Validation
  const validationBanner = document.getElementById('validation-banner');
  const validationMessages = document.getElementById('validation-messages');

  // Inspector
  const insModName = document.getElementById('ins-mod-name');
  const insModId = document.getElementById('ins-mod-id');
  const insStatusBadge = document.getElementById('ins-status-badge');
  const insSrcRev = document.getElementById('ins-src-rev');
  const insArtRev = document.getElementById('ins-art-rev');
  const insDuration = document.getElementById('ins-duration');
  const insFailSim = document.getElementById('ins-fail-sim');
  const insDepsList = document.getElementById('ins-deps-list');
  const insDependentsList = document.getElementById('ins-dependents-list');

  // Simulator
  const moduleSelect = document.getElementById('module-select');
  const btnMarkChanged = document.getElementById('btn-mark-changed');
  const btnToggleFailure = document.getElementById('btn-toggle-failure');

  // Plan & Exec
  const btnPlan = document.getElementById('btn-plan');
  const planStepCount = document.getElementById('plan-step-count');
  const planEstDuration = document.getElementById('plan-est-duration');
  const planValidity = document.getElementById('plan-validity');
  const planStepsList = document.getElementById('plan-steps-list');

  const btnBuild = document.getElementById('btn-build');
  const execStatusPill = document.getElementById('exec-status-pill');
  const execTimeText = document.getElementById('exec-time-text');
  const execCompletedCount = document.getElementById('exec-completed-count');
  const execFailedCount = document.getElementById('exec-failed-count');
  const execBlockedCount = document.getElementById('exec-blocked-count');
  const execStepsList = document.getElementById('exec-steps-list');

  const btnReset = document.getElementById('btn-reset');
  const btnClearLog = document.getElementById('btn-clear-log');
  const terminalLog = document.getElementById('terminal-log');
  const dependencySvg = document.getElementById('dependency-svg');

  // Logging Helper
  function logEvent(action, message, isError = false) {
    const entry = document.createElement('div');
    entry.className = 'log-entry';
    const time = new Date().toLocaleTimeString();
    entry.innerHTML = `
      <span class="log-time">[${time}]</span>
      <span class="log-action">${action}</span>
      <span class="${isError ? 'log-error' : 'log-msg'}">${message}</span>
    `;
    terminalLog.appendChild(entry);
    terminalLog.scrollTop = terminalLog.scrollHeight;
  }

  // API Call Helpers (using relative paths)
  async function apiFetch(endpoint, options = {}) {
    try {
      const response = await fetch(endpoint, options);
      if (!response.ok) {
        const errorText = await response.text();
        throw new Error(errorText || `HTTP ${response.status}`);
      }
      return await response.json();
    } catch (err) {
      logEvent('API ERROR', `${endpoint} failed: ${err.message}`, true);
      throw err;
    }
  }

  // Check Server Health
  async function checkHealth() {
    try {
      const data = await apiFetch('api/health');
      serverStatusPill.className = 'status-pill';
      serverStatusText.textContent = 'ONLINE';
    } catch (err) {
      serverStatusPill.className = 'status-pill offline';
      serverStatusText.textContent = 'OFFLINE';
    }
  }

  // Refresh Full State & Graph
  async function refreshAll() {
    try {
      const [state, graph] = await Promise.all([
        apiFetch('api/state'),
        apiFetch('api/graph')
      ]);

      currentModules = state.modules || [];
      currentGraph = graph || { modules: [], validation: {} };
      lastPlan = state.lastPlan;
      latestResult = state.latestResult;

      updateMetrics(state);
      updateValidation(graph.validation);
      populateSelectOptions();
      updateInspector();
      renderGraph();
      renderPlan(lastPlan);
      renderExecutionResult(latestResult);

      projectRevBadge.textContent = `#${state.projectRevision || 1}`;
      updateBuildBadge(latestResult ? latestResult.status : 'idle');
    } catch (err) {
      console.error('Failed to refresh state:', err);
    }
  }

  function updateBuildBadge(status) {
    lastBuildStatusBadge.className = `stat-val status-${status}`;
    lastBuildStatusBadge.textContent = status.toUpperCase();
  }

  // Update Overview Metrics
  function updateMetrics(state) {
    if (!state.metrics) return;
    metricTotal.textContent = state.metrics.totalModules ?? currentModules.length;
    metricModified.textContent = state.metrics.modifiedModules ?? 0;
    metricAffected.textContent = state.metrics.affectedModules ?? 0;
    metricCached.textContent = state.metrics.cachedArtifacts ?? 0;
    metricDuration.textContent = `${state.metrics.lastBuildDurationMs ?? 0} ms`;
  }

  // Validation Diagnostics Banner
  function updateValidation(validation) {
    if (!validation || validation.isValid) {
      validationBanner.className = 'validation-banner hidden';
      validationMessages.innerHTML = '';
      return;
    }

    validationBanner.className = 'validation-banner';
    let html = '<strong>Graph Invalidation Warnings:</strong><ul style="margin-left: 18px; margin-top: 4px;">';
    if (validation.diagnostics && validation.diagnostics.length > 0) {
      for (const diag of validation.diagnostics) {
        html += `<li>${diag}</li>`;
      }
    } else if (validation.hasCycle) {
      html += `<li>${validation.cycleDescription || 'Detected cyclic dependency relationship'}</li>`;
    }
    html += '</ul>';
    validationMessages.innerHTML = html;
  }

  // Populate Select Dropdown
  function populateSelectOptions() {
    const prevVal = moduleSelect.value || selectedModuleId;
    moduleSelect.innerHTML = '';

    for (const mod of currentModules) {
      const opt = document.createElement('option');
      opt.value = mod.id;
      opt.textContent = `${mod.id} (${mod.name})`;
      if (mod.id === prevVal) opt.selected = true;
      moduleSelect.appendChild(opt);
    }

    if (!selectedModuleId && currentModules.length > 0) {
      selectedModuleId = currentModules[0].id;
    }
  }

  // Module Inspector
  function updateInspector() {
    const mod = currentModules.find(m => m.id === selectedModuleId);
    if (!mod) {
      insModName.textContent = 'None';
      insModId.textContent = '';
      insStatusBadge.textContent = 'UNKNOWN';
      return;
    }

    insModName.textContent = mod.name;
    insModId.textContent = `(${mod.id})`;
    insSrcRev.textContent = `rev ${mod.sourceRevision}`;
    insArtRev.textContent = `rev ${mod.artifactRevision}`;
    insDuration.textContent = `${mod.durationMs} ms`;
    insFailSim.textContent = mod.simulateFailure ? 'ENABLED (Will Fail)' : 'Disabled';
    insFailSim.className = mod.simulateFailure ? 'ins-prop-val text-rose' : 'ins-prop-val';

    // Status badge
    const isAffected = lastPlan && lastPlan.affectedModules && lastPlan.affectedModules.includes(mod.id);
    if (mod.sourceRevision !== mod.artifactRevision) {
      insStatusBadge.className = 'badge badge-modified';
      insStatusBadge.textContent = 'MODIFIED';
    } else if (isAffected) {
      insStatusBadge.className = 'badge badge-affected';
      insStatusBadge.textContent = 'AFFECTED';
    } else {
      insStatusBadge.className = 'badge badge-cached';
      insStatusBadge.textContent = 'CACHED';
    }

    // Dependencies
    insDepsList.innerHTML = '';
    if (mod.dependencies && mod.dependencies.length > 0) {
      for (const depId of mod.dependencies) {
        const tag = document.createElement('span');
        tag.className = 'dep-tag';
        tag.textContent = depId;
        tag.onclick = () => selectModule(depId);
        insDepsList.appendChild(tag);
      }
    } else {
      insDepsList.textContent = 'None (Root target)';
    }

    // Dependents
    const graphNode = currentGraph.modules.find(m => m.id === mod.id);
    insDependentsList.innerHTML = '';
    if (graphNode && graphNode.dependents && graphNode.dependents.length > 0) {
      for (const dep of graphNode.dependents) {
        const tag = document.createElement('span');
        tag.className = 'dep-tag';
        tag.textContent = dep;
        tag.onclick = () => selectModule(dep);
        insDependentsList.appendChild(tag);
      }
    } else {
      insDependentsList.textContent = 'None (Leaf target)';
    }
  }

  function selectModule(id) {
    selectedModuleId = id;
    moduleSelect.value = id;
    updateInspector();
    renderGraph();
  }

  // Render Interactive SVG Graph
  function renderGraph() {
    dependencySvg.innerHTML = '';

    const width = dependencySvg.clientWidth || 700;
    const height = 420;

    // Fixed or computed coordinates for the default 10 modules
    // Topological columns layout:
    // Col 0: core_types
    // Col 1: math_utils, string_utils, storage
    // Col 2: parser, build_graph
    // Col 3: config, reporter
    // Col 4: build_cli
    // Col 5: app
    const layout = {
      'core_types':   { col: 0, row: 1 },
      'math_utils':   { col: 1, row: 0 },
      'string_utils': { col: 1, row: 1 },
      'storage':      { col: 1, row: 2 },
      'parser':       { col: 2, row: 1 },
      'build_graph':  { col: 2, row: 2 },
      'config':       { col: 3, row: 0.5 },
      'reporter':     { col: 3, row: 2 },
      'build_cli':    { col: 4, row: 1 },
      'app':          { col: 5, row: 1.5 }
    };

    const nodeWidth = 110;
    const nodeHeight = 52;
    const colSpacing = (width - 140) / 5;
    const rowSpacing = 95;
    const startX = 20;
    const startY = 40;

    // Define SVG marker for arrows
    const defs = document.createElementNS('http://www.w3.org/2000/svg', 'defs');
    defs.innerHTML = `
      <marker id="arrow" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
        <path d="M 0 1 L 10 5 L 0 9 z" fill="#475569" />
      </marker>
      <marker id="arrow-active" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
        <path d="M 0 1 L 10 5 L 0 9 z" fill="#38bdf8" />
      </marker>
    `;
    dependencySvg.appendChild(defs);

    // Compute node center positions
    const positions = {};
    for (const mod of currentModules) {
      const posInfo = layout[mod.id] || { col: 2, row: 1 };
      const x = startX + posInfo.col * colSpacing;
      const y = startY + posInfo.row * rowSpacing;
      positions[mod.id] = { x, y, cx: x + nodeWidth / 2, cy: y + nodeHeight / 2 };
    }

    // Draw Edges (Directed: from dependent to dependency that it requires)
    for (const mod of currentModules) {
      if (!positions[mod.id]) continue;
      const src = positions[mod.id];

      for (const depId of mod.dependencies) {
        if (!positions[depId]) continue;
        const dst = positions[depId];

        const isRelated = mod.id === selectedModuleId || depId === selectedModuleId;
        const path = document.createElementNS('http://www.w3.org/2000/svg', 'path');

        // Bezier curve between nodes
        const midX = (src.cx + dst.cx) / 2;
        const d = `M ${src.cx} ${src.cy} C ${midX} ${src.cy}, ${midX} ${dst.cy}, ${dst.cx} ${dst.cy}`;

        path.setAttribute('d', d);
        path.setAttribute('class', `graph-edge ${isRelated ? 'edge-active' : ''}`);
        path.setAttribute('marker-end', isRelated ? 'url(#arrow-active)' : 'url(#arrow)');
        dependencySvg.appendChild(path);
      }
    }

    // Draw Nodes
    for (const mod of currentModules) {
      if (!positions[mod.id]) continue;
      const pos = positions[mod.id];

      const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
      g.setAttribute('class', 'graph-node');
      g.setAttribute('transform', `translate(${pos.x}, ${pos.y})`);

      const rect = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
      rect.setAttribute('width', nodeWidth);
      rect.setAttribute('height', nodeHeight);

      // Determine status class
      let statusClass = 'status-cached';
      const isAffected = lastPlan && lastPlan.affectedModules && lastPlan.affectedModules.includes(mod.id);
      if (mod.sourceRevision !== mod.artifactRevision) {
        statusClass = 'status-modified';
      } else if (isAffected) {
        statusClass = 'status-affected';
      }
      if (mod.id === selectedModuleId) {
        statusClass += ' selected';
      }

      rect.setAttribute('class', `node-rect ${statusClass}`);

      const title = document.createElementNS('http://www.w3.org/2000/svg', 'text');
      title.setAttribute('x', nodeWidth / 2);
      title.setAttribute('y', 22);
      title.setAttribute('class', 'node-title');
      title.textContent = mod.id;

      const sub = document.createElementNS('http://www.w3.org/2000/svg', 'text');
      sub.setAttribute('x', nodeWidth / 2);
      sub.setAttribute('y', 40);
      sub.setAttribute('class', 'node-sub');
      const revText = mod.sourceRevision === mod.artifactRevision ? `cached (r${mod.artifactRevision})` : `src: r${mod.sourceRevision}`;
      sub.textContent = revText;

      g.appendChild(rect);
      g.appendChild(title);
      g.appendChild(sub);

      g.onclick = () => selectModule(mod.id);

      dependencySvg.appendChild(g);
    }
  }

  // Render Build Plan Steps
  function renderPlan(plan) {
    if (!plan || !plan.steps || plan.steps.length === 0) {
      planStepCount.textContent = '0';
      planEstDuration.textContent = '0 ms';
      planValidity.textContent = plan && !plan.isValid ? 'Invalid' : 'Idle';
      planStepsList.innerHTML = '<div class="empty-state">No active build plan. Click "Generate Build Plan" or modify a source module.</div>';
      return;
    }

    planStepCount.textContent = plan.steps.length;
    planEstDuration.textContent = `${plan.totalEstimatedDurationMs || 0} ms`;
    planValidity.textContent = plan.isValid ? 'Valid' : 'Errors detected';
    planValidity.className = plan.isValid ? 'summary-val text-emerald' : 'summary-val text-rose';

    planStepsList.innerHTML = '';
    plan.steps.forEach((step, idx) => {
      const card = document.createElement('div');
      card.className = 'step-card';
      const mod = currentModules.find(m => m.id === step.moduleId);
      const isRebuild = mod && mod.sourceRevision !== mod.artifactRevision;

      card.innerHTML = `
        <div class="step-info">
          <span class="step-order">${idx + 1}</span>
          <div>
            <span class="step-name">${step.moduleId}</span>
            <span style="color: var(--text-muted); font-size: 11px;">(${mod ? mod.name : ''})</span>
          </div>
        </div>
        <div class="step-meta">
          <span class="badge ${isRebuild ? 'badge-modified' : 'badge-affected'}">${isRebuild ? 'MODIFIED' : 'AFFECTED'}</span>
          <span>~${step.durationMs} ms</span>
        </div>
      `;
      planStepsList.appendChild(card);
    });
  }

  // Render Execution Outcomes
  function renderExecutionResult(result) {
    if (!result || result.status === 'idle' || !result.steps || result.steps.length === 0) {
      execStatusPill.className = 'status-pill-static';
      execStatusPill.textContent = 'IDLE';
      execTimeText.textContent = '0 ms';
      execCompletedCount.textContent = '0 completed';
      execFailedCount.textContent = '0 failed';
      execBlockedCount.textContent = '0 blocked';
      execStepsList.innerHTML = '<div class="empty-state">Execute a build plan to view step outcomes and artifact revision updates.</div>';
      return;
    }

    execStatusPill.className = `status-pill-static ${result.status}`;
    execStatusPill.textContent = result.status.toUpperCase();
    execTimeText.textContent = `${result.totalDurationMs || 0} ms`;

    execCompletedCount.textContent = `${result.completedModules ? result.completedModules.length : 0} completed`;
    execFailedCount.textContent = `${result.failedModules ? result.failedModules.length : 0} failed`;
    execBlockedCount.textContent = `${result.blockedModules ? result.blockedModules.length : 0} blocked`;

    execStepsList.innerHTML = '';
    result.steps.forEach((step, idx) => {
      const card = document.createElement('div');
      card.className = 'step-card';

      let statusBadgeClass = 'badge-subtle';
      if (step.status === 'completed') statusBadgeClass = 'badge-cached';
      else if (step.status === 'failed') statusBadgeClass = 'badge-modified text-rose';
      else if (step.status === 'blocked') statusBadgeClass = 'badge-modified text-amber';

      card.innerHTML = `
        <div class="step-info">
          <span class="step-order">${idx + 1}</span>
          <div>
            <span class="step-name">${step.moduleId}</span>
            ${step.errorMessage ? `<span class="step-error-msg">${step.errorMessage}</span>` : ''}
          </div>
        </div>
        <div class="step-meta">
          <span class="badge ${statusBadgeClass}">${step.status.toUpperCase()}</span>
          <span>${step.durationMs} ms</span>
        </div>
      `;
      execStepsList.appendChild(card);
    });
  }

  // Event Handlers
  moduleSelect.addEventListener('change', (e) => {
    selectModule(e.target.value);
  });

  btnMarkChanged.addEventListener('click', async () => {
    const modId = moduleSelect.value;
    if (!modId) return;

    btnMarkChanged.disabled = true;
    try {
      logEvent('SOURCE EDIT', `Marked module '${modId}' modified. Requesting invalidation calculation...`);
      const updatedState = await apiFetch('api/source-change', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ moduleId: modId, toggleFailure: false })
      });
      logEvent('STATE UPDATE', `Project revision incremented to #${updatedState.projectRevision}. Plan recalculated.`);
      await refreshAll();
    } catch (err) {
      logEvent('ERROR', `Failed to mark source changed: ${err.message}`, true);
    } finally {
      btnMarkChanged.disabled = false;
    }
  });

  btnToggleFailure.addEventListener('click', async () => {
    const modId = moduleSelect.value;
    if (!modId) return;

    btnToggleFailure.disabled = true;
    try {
      const updatedState = await apiFetch('api/source-change', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ moduleId: modId, toggleFailure: true })
      });
      const mod = updatedState.modules.find(m => m.id === modId);
      logEvent('CONFIG SIM', `Toggled failure simulation on '${modId}': ${mod.simulateFailure ? 'ENABLED' : 'DISABLED'}`);
      await refreshAll();
    } catch (err) {
      logEvent('ERROR', `Failed to toggle failure simulation: ${err.message}`, true);
    } finally {
      btnToggleFailure.disabled = false;
    }
  });

  btnPlan.addEventListener('click', async () => {
    btnPlan.disabled = true;
    try {
      logEvent('BUILD PLANNER', `Requesting incremental build plan for module '${selectedModuleId}'...`);
      const plan = await apiFetch('api/plan', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ moduleId: selectedModuleId })
      });
      lastPlan = plan;
      renderPlan(lastPlan);
      logEvent('PLAN GENERATED', `Calculated ${plan.steps.length} steps. Estimated compile time: ${plan.totalEstimatedDurationMs}ms.`);
      await refreshAll();
    } catch (err) {
      logEvent('ERROR', `Failed to generate build plan: ${err.message}`, true);
    } finally {
      btnPlan.disabled = false;
    }
  });

  btnBuild.addEventListener('click', async () => {
    btnBuild.disabled = true;
    try {
      logEvent('BUILD EXECUTOR', `Executing simulated build plan in C++ engine...`);
      const result = await apiFetch('api/build', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' }
      });
      latestResult = result;
      renderExecutionResult(latestResult);
      logEvent('BUILD FINISHED', `Execution outcome: ${result.status.toUpperCase()} (${result.totalDurationMs}ms elapsed).`);
      await refreshAll();
    } catch (err) {
      logEvent('ERROR', `Failed to execute build: ${err.message}`, true);
    } finally {
      btnBuild.disabled = false;
    }
  });

  btnReset.addEventListener('click', async () => {
    if (!confirm('Reset project back to the default 10-module demonstration state?')) return;
    try {
      logEvent('PROJECT RESET', `Resetting dependency graph to demonstration project defaults...`);
      await apiFetch('api/reset', { method: 'POST' });
      selectedModuleId = 'core_types';
      await refreshAll();
      logEvent('RESET DONE', `Project restored to initial state.`);
    } catch (err) {
      logEvent('ERROR', `Failed to reset project: ${err.message}`, true);
    }
  });

  btnClearLog.addEventListener('click', () => {
    terminalLog.innerHTML = '';
    logEvent('LOG CLEARED', 'Diagnostics log reset.');
  });

  // Initial Load
  logEvent('INIT', 'BuildMesh web application loaded.');
  checkHealth();
  refreshAll();

  // Periodic health check
  setInterval(checkHealth, 5000);
});
