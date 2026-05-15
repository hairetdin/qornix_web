(function () {
  const $ = (id) => document.getElementById(id);
  const api = '/api/dynamic/schema';
  let lastPlan = null;
  let lastCurrentSchemaXml = '';
  const pretty = (v) => typeof v === 'string' ? v : JSON.stringify(v, null, 2);
  const xml = () => $('qdx-xml').value || '';
  async function request(path, options) {
    const res = await fetch(api + path, options || {});
    const text = await res.text();
    try { return JSON.parse(text); } catch (_) { return { ok: res.ok, raw: text }; }
  }
  function show(id, value, error) {
    const node = $(id);
    node.textContent = pretty(value);
    node.classList.toggle('error', !!error);
    node.classList.toggle('ok', !error);
  }
  $('qdx-file').addEventListener('change', async (e) => {
    const file = e.target.files && e.target.files[0];
    if (file) $('qdx-xml').value = await file.text();
  });
  $('qdx-load-demo-schema-btn')?.addEventListener('click', async () => {
    try {
      const res = await fetch('/static/demo_schema.xml');
      if (!res.ok) throw new Error(`${res.status} ${res.statusText}`);
      $('qdx-xml').value = await res.text();
      show('qdx-validation', { ok: true, message: 'Bundled demo schema loaded. Next: Validate -> Diff -> Plan -> Apply live plan.' }, false);
    } catch (error) {
      show('qdx-validation', { ok: false, message: 'Failed to load bundled demo schema', error: error.message }, true);
    }
  });
  $('qdx-export-btn').addEventListener('click', async () => {
    const data = await request('/export');
    const exportedXml = (typeof data === 'string') ? data : (data.xml || data.raw || '');
    if (exportedXml) $('qdx-xml').value = exportedXml;
    lastCurrentSchemaXml = exportedXml;
    const saveButton = $('qdx-save-current-schema-btn');
    if (saveButton) saveButton.disabled = !lastCurrentSchemaXml;
    show('qdx-current-schema', exportedXml || data, !data.ok && !data.raw && !data.xml);
    show('qdx-current-schema-status', lastCurrentSchemaXml
      ? { ok: true, message: 'Current schema loaded. You can now save it to your local disk.' }
      : { ok: false, message: 'Could not fetch the current schema XML.' }, !lastCurrentSchemaXml);
  });
  $('qdx-save-current-schema-btn')?.addEventListener('click', () => {
    if (!lastCurrentSchemaXml) {
      show('qdx-current-schema-status', { ok: false, message: 'Load the current database schema first.' }, true);
      return;
    }
    const timestamp = new Date().toISOString().replace(/[:.]/g, '-');
    const blob = new Blob([lastCurrentSchemaXml], { type: 'application/xml;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = `qornix-current-schema-${timestamp}.xml`;
    document.body.appendChild(link);
    link.click();
    link.remove();
    URL.revokeObjectURL(url);
    show('qdx-current-schema-status', { ok: true, message: `Schema saved as ${link.download}` }, false);
  });
  $('qdx-validate-btn').addEventListener('click', async () => {
    const data = await request('/validate', { method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({ xml: xml() }) });
    show('qdx-validation', data, data.ok === false);
  });
  $('qdx-diff-btn').addEventListener('click', async () => {
    const data = await request('/diff', { method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({ xml: xml() }) });
    show('qdx-diff', data.diff || data.operations || data, data.ok === false);
  });
  $('qdx-plan-btn').addEventListener('click', async () => {
    const data = await request('/plan', { method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({ xml: xml() }) });
    lastPlan = data.ok === false ? null : data;
    $('qdx-apply-btn').disabled = !lastPlan;
    show('qdx-plan-summary', { plan_id: data.plan_id || data.planId, message: data.message }, data.ok === false);
    show('qdx-risk', data.risks || data.plan || data, data.ok === false);
    show('qdx-sql', data.sql_preview || data.sqlPreview || data.sql || data, data.ok === false);
  });
  $('qdx-apply-btn').addEventListener('click', async () => {
    if (!lastPlan) return;
    const dryRunOnly = !!$('qdx-dry-run-only')?.checked;
    const data = await request('/apply', { method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({
      xml: xml(),
      plan_id: lastPlan.plan_id || lastPlan.planId,
      destructive_confirmed: $('qdx-confirm-destructive').checked,
      manual_review_confirmed: $('qdx-confirm-manual').checked,
      dry_run: dryRunOnly,
      applied_by: dryRunOnly ? 'schema-manager-dry-run' : 'schema-manager-live-apply'
    }) });
    const didNotApply = data && (data.dry_run === true || (data.result && data.result.dryRun === true));
    if (data && data.ok === false && String(data.message || '').includes('Unsupported operations')) {
      data.hint = 'The current database already differs from the selected schema or the selected XML contains advanced objects. For the first demo run, click Reset demo database, then Load demo schema and Apply live plan again.';
    }
    show('qdx-apply-result', data, data.ok === false || didNotApply);
  });
  $('qdx-reset-demo-db-btn')?.addEventListener('click', async () => {
    if (!window.confirm('Reset demo database? This deletes app.sqlite3, exported schema and schema history for this generated app.')) return;
    try {
      const res = await fetch('/api/demo/reset', { method: 'POST' });
      const text = await res.text();
      let data;
      try { data = JSON.parse(text); } catch (_) { data = { ok: res.ok, raw: text }; }
      show('qdx-apply-result', data, !res.ok || data.ok === false);
      show('qdx-current-schema', 'Database reset. Click "Load current DB schema" to confirm it is empty, then Load demo schema -> Validate -> Diff -> Plan -> Apply live plan.', false);
      lastCurrentSchemaXml = '';
      const saveButton = $('qdx-save-current-schema-btn');
      if (saveButton) saveButton.disabled = true;
      show('qdx-current-schema-status', { ok: true, message: 'Database reset. Load the current schema again before saving.' }, false);
      lastPlan = null;
      $('qdx-apply-btn').disabled = true;
    } catch (error) {
      show('qdx-apply-result', { ok: false, message: 'Failed to reset database', error: error.message }, true);
    }
  });
  $('qdx-history-btn').addEventListener('click', async () => {
    const data = await request('/history');
    show('qdx-history', data, data.ok === false);
  });
})();
