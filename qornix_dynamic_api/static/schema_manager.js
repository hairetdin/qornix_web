(function () {
  const $ = (id) => document.getElementById(id);
  const api = '/api/dynamic/schema';
  let lastPlan = null;
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
  $('qdx-export-btn').addEventListener('click', async () => {
    const data = await request('/export');
    if (typeof data === 'string') $('qdx-xml').value = data;
    if (data.xml) $('qdx-xml').value = data.xml;
    show('qdx-current-schema', data.xml || data.raw || data, !data.ok && !data.raw);
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
    const data = await request('/apply', { method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({
      xml: xml(),
      plan_id: lastPlan.plan_id || lastPlan.planId,
      destructive_confirmed: $('qdx-confirm-destructive').checked,
      manual_review_confirmed: $('qdx-confirm-manual').checked
    }) });
    show('qdx-apply-result', data, data.ok === false);
  });
  $('qdx-history-btn').addEventListener('click', async () => {
    const data = await request('/history');
    show('qdx-history', data, data.ok === false);
  });
})();
