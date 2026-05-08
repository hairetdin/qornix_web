(function () {
  'use strict';

  const DEFAULT_SCHEMA = `<?xml version="1.0" encoding="UTF-8"?>
<Application schemaFormatVersion="1.0">
  <Name>Qornix Dynamic API Demo</Name>
  <Version>1.0.0</Version>
  <Description>Bundled first-run demo schema for Qornix Web Dynamic API. Apply it from Schema Manager, seed demo rows, then explore Query Builder and Table Browser.</Description>
  <Configuration>
    <DbConnectionString>app.sqlite3</DbConnectionString>
    <LogLevel>INFO</LogLevel>
    <Timeout>30</Timeout>
    <DatabaseEngine>sqlite</DatabaseEngine>
  </Configuration>
  <DataStructure>
    <Entity name="Categories" tableName="categories">
      <Field name="id" type="INTEGER" nullable="false" primaryKey="true" unique="false" autoIncrement="true" />
      <Field name="name" type="TEXT" nullable="false" primaryKey="false" unique="false" autoIncrement="false" />
      <Field name="description" type="TEXT" nullable="true" primaryKey="false" unique="false" autoIncrement="false" />
    </Entity>
    <Entity name="Customers" tableName="customers">
      <Field name="id" type="INTEGER" nullable="false" primaryKey="true" unique="false" autoIncrement="true" />
      <Field name="name" type="TEXT" nullable="false" primaryKey="false" unique="false" autoIncrement="false" />
      <Field name="email" type="TEXT" nullable="false" primaryKey="false" unique="false" autoIncrement="false" />
      <Field name="phone" type="TEXT" nullable="true" primaryKey="false" unique="false" autoIncrement="false" />
      <Field name="city" type="TEXT" nullable="true" primaryKey="false" unique="false" autoIncrement="false" />
    </Entity>
    <Entity name="Products" tableName="products">
      <Field name="id" type="INTEGER" nullable="false" primaryKey="true" unique="false" autoIncrement="true" />
      <Field name="category_id" type="INTEGER" nullable="true" primaryKey="false" unique="false" autoIncrement="false" />
      <Field name="name" type="TEXT" nullable="false" primaryKey="false" unique="false" autoIncrement="false" />
      <Field name="description" type="TEXT" nullable="true" primaryKey="false" unique="false" autoIncrement="false" />
      <Field name="price" type="REAL" nullable="false" primaryKey="false" unique="false" autoIncrement="false" />
      <Field name="stock" type="INTEGER" nullable="false" primaryKey="false" unique="false" autoIncrement="false" />
      <ForeignKeyField name="category_id" type="FOREIGN_KEY" nullable="true" primaryKey="false" unique="false" references="categories" toField="id" onDelete="SET NULL" onUpdate="NO_ACTION" />
    </Entity>
    <Entity name="Orders" tableName="orders">
      <Field name="id" type="INTEGER" nullable="false" primaryKey="true" unique="false" autoIncrement="true" />
      <Field name="customer_id" type="INTEGER" nullable="false" primaryKey="false" unique="false" autoIncrement="false" />
      <Field name="product_id" type="INTEGER" nullable="false" primaryKey="false" unique="false" autoIncrement="false" />
      <Field name="quantity" type="INTEGER" nullable="false" primaryKey="false" unique="false" autoIncrement="false" />
      <Field name="status" type="TEXT" nullable="false" primaryKey="false" unique="false" autoIncrement="false" />
      <Field name="total_amount" type="REAL" nullable="false" primaryKey="false" unique="false" autoIncrement="false" />
      <ForeignKeyField name="customer_id" type="FOREIGN_KEY" nullable="false" primaryKey="false" unique="false" references="customers" toField="id" onDelete="NO_ACTION" onUpdate="NO_ACTION" />
      <ForeignKeyField name="product_id" type="FOREIGN_KEY" nullable="false" primaryKey="false" unique="false" references="products" toField="id" onDelete="NO_ACTION" onUpdate="NO_ACTION" />
    </Entity>
  </DataStructure>
</Application>
`;

  const state = {
    fields: [], where: [], filters: [], joins: [], groupBy: [], having: [], orderBy: [],
    tableFields: [], row: {}, rowTable: '', rowId: '', activeRecipe: ''
  };

  const $ = (id) => document.getElementById(id);
  const page = () => document.body.dataset.qdbPage || '';
  const compact = (s) => String(s || '').replace(/\s+/g, ' ').trim();

  function asArray(payload) {
    if (!payload) return [];
    if (Array.isArray(payload.items)) return payload.items;
    if (Array.isArray(payload.data)) return payload.data;
    if (payload.allowlist && Array.isArray(payload.allowlist.tables)) return payload.allowlist.tables;
    return [];
  }
  function escapeHtml(value) { return String(value ?? '').replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;').replaceAll('"','&quot;').replaceAll("'",'&#039;'); }
  function parseTypedValue(value) {
    const raw = String(value ?? '').trim();
    if (raw === '') return '';
    if (raw === 'null') return null;
    if (raw === 'true') return true;
    if (raw === 'false') return false;
    if (/^-?\d+$/.test(raw)) return Number.parseInt(raw, 10);
    if (/^-?\d+\.\d+$/.test(raw)) return Number.parseFloat(raw);
    return raw.replace(/^['"]|['"]$/g, '');
  }
  async function fetchText(url, options) { const response = await fetch(url, options); const text = await response.text(); if (!response.ok) { const error = new Error(`${response.status} ${response.statusText}`); error.text = text; error.status = response.status; throw error; } return text; }
  async function fetchJson(url, options) { const response = await fetch(url, options); const text = await response.text(); let data; try { data = text ? JSON.parse(text) : {}; } catch (_) { data = { ok: response.ok, raw: text }; } if (!response.ok) { const message = data.message || data.error || data.code || `${response.status} ${response.statusText}`; const error = new Error(message); error.payload = data; error.status = response.status; throw error; } return data; }
  function setResult(payload, ok = true) { const box = $('qdb-result-json'); if (!box) return; box.textContent = typeof payload === 'string' ? payload : JSON.stringify(payload, null, 2); box.classList.toggle('qdb-ok', ok); box.classList.toggle('qdb-error', !ok); }
  function setStatus(message, kind = 'muted') { const status = $('qdb-status'); if (!status) return; status.textContent = message; status.className = `qdb-status ${kind}`; }
  function setPreview(payload) { const preview = $('qdb-json-preview'); if (preview) preview.textContent = typeof payload === 'string' ? payload : JSON.stringify(payload, null, 2); }
  function getRowData(payload) { if (!payload) return null; if (Array.isArray(payload.data)) return payload.data[0] || null; if (payload.data && typeof payload.data === 'object') return payload.data; if (payload.row && typeof payload.row === 'object') return payload.row; return null; }

  function selectedTable() { return ($('qdb-table')?.value || $('qdb-table-browser-table')?.value || '').trim(); }
  function currentMethod() { return ($('qdb-method')?.value || 'GET').trim().toUpperCase(); }
  function uniquePush(list, value) { const v = String(value || '').trim(); if (v && !list.includes(v)) list.push(v); }
  function removeAt(listName, index) { state[listName].splice(index, 1); renderChips(listName); }
  const chipConfig = {
    fields: ['qdb-fields-list', (x) => x], where: ['qdb-where-list', (x) => `${x.field} ${x.op} ${JSON.stringify(x.value)}`], filters: ['qdb-filter-list', (x) => x], joins: ['qdb-join-list', (x) => x], groupBy: ['qdb-group-list', (x) => x], having: ['qdb-having-list', (x) => x], orderBy: ['qdb-order-list', (x) => x]
  };
  function renderChips(kind) {
    const cfg = chipConfig[kind]; if (!cfg) return; const [targetId, labeler] = cfg; const target = $(targetId); if (!target) return;
    const items = state[kind];
    target.innerHTML = items.map((item, index) => `<span class="qdb-chip">${escapeHtml(labeler(item))}<button type="button" data-chip-kind="${kind}" data-chip-index="${index}" aria-label="Remove">x</button></span>`).join('');
    target.querySelectorAll('button[data-chip-kind]').forEach((button) => button.addEventListener('click', () => removeAt(button.dataset.chipKind, Number(button.dataset.chipIndex))));
    refreshPreview();
  }
  function renderAllChips() { Object.keys(chipConfig).forEach(renderChips); }

  function addField() { const input = $('qdb-field-input'); uniquePush(state.fields, input?.value); if (input) input.value = ''; renderChips('fields'); }
  function addWhere() { const field = ($('qdb-where-field')?.value || '').trim(); if (!field) return; state.where.push({ field, op: ($('qdb-where-op')?.value || 'eq').trim(), value: parseTypedValue($('qdb-where-value')?.value || '') }); if ($('qdb-where-field')) $('qdb-where-field').value = ''; if ($('qdb-where-value')) $('qdb-where-value').value = ''; renderChips('where'); }
  function addFilter() { const input = $('qdb-filter-input'); uniquePush(state.filters, input?.value); if (input) input.value = ''; renderChips('filters'); }
  function addJoin() { const input = $('qdb-join-input'); uniquePush(state.joins, input?.value); if (input) input.value = ''; renderChips('joins'); }
  function addGroup() { const input = $('qdb-group-input'); uniquePush(state.groupBy, input?.value); if (input) input.value = ''; renderChips('groupBy'); }
  function addHaving() { const input = $('qdb-having-input'); uniquePush(state.having, input?.value); if (input) input.value = ''; renderChips('having'); }
  function addOrder() { const field = ($('qdb-order-field')?.value || '').trim(); const dir = ($('qdb-order-direction')?.value || 'ASC').trim(); if (!field) return; uniquePush(state.orderBy, `${field} ${dir}`); if ($('qdb-order-field')) $('qdb-order-field').value = ''; renderChips('orderBy'); }

  function buildQueryPayload() {
    const table = selectedTable();
    const method = currentMethod();
    const limit = Math.max(1, Number.parseInt($('qdb-limit')?.value || $('qdb-table-browser-limit')?.value || '100', 10) || 100);
    const payload = { method, table };
    if (method === 'GET' || method === 'DELETE') {
      payload.query = { limit };
      if (state.fields.length) payload.query.fields = state.fields.slice();
      if (state.where.length) payload.query.where = state.where.map((x) => ({ ...x }));
      if (state.filters.length) payload.query.filter = state.filters.slice();
      if (state.joins.length) payload.query.join = state.joins.slice();
      if (state.groupBy.length) payload.query.group_by = state.groupBy.slice();
      if (state.having.length) payload.query.having = state.having.slice();
      if (state.orderBy.length) payload.query.order_by = state.orderBy.slice();
    }
    if (method === 'POST' || method === 'PUT' || method === 'PATCH') {
      let data = {};
      try { data = JSON.parse($('qdb-data-json')?.value || '{}'); } catch (_) { data = { _invalid_json: $('qdb-data-json')?.value || '' }; }
      payload.data = data;
      if ((method === 'PUT' || method === 'PATCH') && (state.where.length || state.filters.length)) {
        payload.query = {};
        if (state.where.length) payload.query.where = state.where.map((x) => ({ ...x }));
        if (state.filters.length) payload.query.filter = state.filters.slice();
      }
    }
    return payload;
  }
  function refreshPreview() { if ($('qdb-json-preview') && (page() === 'query-builder' || page() === 'table')) setPreview(buildQueryPayload()); }

  function renderFieldDatalist() { const options = state.tableFields.map((field) => `<option value="${escapeHtml(field)}"></option>`).join(''); ['qdb-field-suggestions','qdb-where-field-suggestions','qdb-order-field-suggestions'].forEach((id) => { const target = $(id); if (target) target.innerHTML = options; }); }
  async function loadTables() {
    const query = $('qdb-table-search')?.value || '';
    try {
      const payload = await fetchJson(`/api/dynamic/meta/tables?q=${encodeURIComponent(query)}&limit=100&offset=0`);
      const tables = asArray(payload).filter(Boolean);
      const options = tables.map((table) => `<option value="${escapeHtml(table)}"></option>`).join('');
      ['qdb-table-suggestions','qdb-table-browser-suggestions'].forEach((id) => { const target = $(id); if (target) target.innerHTML = options; });
      const list = $('qdb-tables-list');
      if (list) {
        list.innerHTML = tables.length ? tables.map((table) => `<button type="button" class="qdb-table-pill" data-table="${escapeHtml(table)}">${escapeHtml(table)}</button>`).join('') : '<div class="qdb-empty">No tables found. Apply the demo schema first.</div>';
        list.querySelectorAll('button[data-table]').forEach((button) => button.addEventListener('click', () => { if ($('qdb-table')) $('qdb-table').value = button.dataset.table; if ($('qdb-table-browser-table')) $('qdb-table-browser-table').value = button.dataset.table; loadFields(button.dataset.table); refreshPreview(); }));
      }
      setStatus(tables.length ? `Loaded tables: ${tables.length}` : 'No schema-backed tables yet.', tables.length ? 'ok' : 'warn');
      return tables;
    } catch (error) { setStatus(`Failed to load tables: ${error.message}`, 'error'); return []; }
  }
  async function loadFields(table = selectedTable()) { if (!table) { state.tableFields = []; renderFieldDatalist(); return []; } try { const payload = await fetchJson(`/api/dynamic/meta/${encodeURIComponent(table)}/fields?q=`); state.tableFields = asArray(payload).filter(Boolean); renderFieldDatalist(); setStatus(`Fields for ${table}: ${state.tableFields.length}`, 'ok'); return state.tableFields; } catch (error) { state.tableFields = []; renderFieldDatalist(); setStatus(`Failed to load fields: ${error.message}`, 'error'); return []; } }

  const sqlExamples = {
    products: "SELECT id, name, price, stock FROM products WHERE price>=40 ORDER BY price DESC LIMIT 10",
    join: "SELECT * FROM orders JOIN products ON orders.product_id=products.id WHERE status='paid' ORDER BY total_amount DESC LIMIT 10",
    aggregate: "SELECT status FROM orders WHERE total_amount>20 GROUP BY status HAVING total_amount>20 ORDER BY status ASC LIMIT 10"
  };
  function findClause(sql, clause) { const m = new RegExp(`\\b${clause}\\b`, 'i').exec(sql); return m ? m.index : -1; }
  function splitList(value) { return String(value || '').split(',').map(compact).filter(Boolean); }
  function splitAnd(value) { return String(value || '').split(/\s+AND\s+/i).map(compact).filter(Boolean); }
  function convertSqlToPayload() {
    const sql = compact($('qdb-sql-input')?.value || '');
    const m = /^SELECT\s+(.+?)\s+FROM\s+([A-Za-z_][\w.]*)\s*(.*)$/i.exec(sql);
    if (!m) { setStatus('SQL converter supports simple SELECT ... FROM ... queries.', 'warn'); return; }
    const fieldsRaw = compact(m[1]); const table = m[2]; let tail = compact(m[3]);
    if ($('qdb-method')) $('qdb-method').value = 'GET'; if ($('qdb-table')) $('qdb-table').value = table;
    state.fields = fieldsRaw === '*' ? [] : splitList(fieldsRaw);
    state.where = []; state.filters = []; state.joins = []; state.groupBy = []; state.having = []; state.orderBy = [];
    const limitMatch = /\bLIMIT\s+(\d+)\b/i.exec(tail); if (limitMatch && $('qdb-limit')) $('qdb-limit').value = limitMatch[1]; tail = tail.replace(/\bLIMIT\s+\d+\b/i, '').trim();
    const orderMatch = /\bORDER\s+BY\s+(.+?)(?=\s+HAVING\b|\s+GROUP\s+BY\b|\s+WHERE\b|$)/i.exec(tail); if (orderMatch) { state.orderBy = splitList(orderMatch[1]); tail = tail.replace(orderMatch[0], '').trim(); }
    const havingMatch = /\bHAVING\s+(.+?)(?=\s+GROUP\s+BY\b|\s+WHERE\b|$)/i.exec(tail); if (havingMatch) { state.having = splitAnd(havingMatch[1]); tail = tail.replace(havingMatch[0], '').trim(); }
    const groupMatch = /\bGROUP\s+BY\s+(.+?)(?=\s+WHERE\b|$)/i.exec(tail); if (groupMatch) { state.groupBy = splitList(groupMatch[1]); tail = tail.replace(groupMatch[0], '').trim(); }
    const whereMatch = /\bWHERE\s+(.+)$/i.exec(tail); if (whereMatch) { state.filters = splitAnd(whereMatch[1]); tail = tail.replace(whereMatch[0], '').trim(); }
    const joinRegex = /\b(?:INNER\s+|LEFT\s+|RIGHT\s+|FULL\s+)?JOIN\s+[A-Za-z_][\w.]*\s+ON\s+.+?(?=\s+(?:INNER\s+|LEFT\s+|RIGHT\s+|FULL\s+)?JOIN\s+|$)/ig;
    const joins = tail.match(joinRegex); if (joins) state.joins = joins.map(compact);
    renderAllChips(); loadFields(table); setStatus('SQL converted to Qornix JSON payload.', 'ok'); refreshPreview();
  }
  function loadSqlExample(name) { if ($('qdb-sql-input')) $('qdb-sql-input').value = sqlExamples[name] || sqlExamples.products; convertSqlToPayload(); }

  async function executeQuery() {
    const payload = buildQueryPayload(); if (!payload.table) { setStatus('Choose a table before running the query.', 'warn'); return; }
    setPreview(payload); setStatus('Running query...', 'muted');
    try { const result = await fetchJson('/api/dynamic', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(payload) }); setResult(result, true); renderResultTable(result, payload.table); setStatus(`Query completed. Rows: ${Array.isArray(result.data) ? result.data.length : (result.count || 0)}`, 'ok'); }
    catch (error) { setResult(error.payload || { error: error.message }, false); renderResultTable(null); setStatus(`Query error: ${error.message}. If this is a demo example, initialize the demo database from the home page first.`, 'error'); }
  }
  function renderResultTable(payload, tableName = selectedTable()) {
    const target = $('qdb-result-table'); if (!target) return; const data = payload && Array.isArray(payload.data) ? payload.data : [];
    if (!data.length) { target.innerHTML = '<div class="qdb-empty">No rows to display.</div>'; return; }
    const columns = Array.from(new Set(data.flatMap((row) => Object.keys(row || {}))));
    target.innerHTML = `<div class="qdb-table-wrap"><table class="qdb-data-table"><thead><tr>${columns.map((col) => `<th>${escapeHtml(col)}</th>`).join('')}<th>Actions</th></tr></thead><tbody>${data.map((row) => `<tr>${columns.map((col) => `<td>${escapeHtml(row[col])}</td>`).join('')}<td>${row.id !== undefined && tableName ? `<a href="/table/${encodeURIComponent(tableName)}/${encodeURIComponent(row.id)}">Open</a>` : ''}</td></tr>`).join('')}</tbody></table></div>`;
  }
  function copyPreview() { const text = $('qdb-json-preview')?.textContent || $('qdb-recipe-body')?.value || ''; navigator.clipboard?.writeText(text); setStatus('JSON copied.', 'ok'); }
  function generateCurl(payload = buildQueryPayload(), endpoint = '/api/dynamic', method = 'POST') { const body = JSON.stringify(payload).replaceAll("'", "'\\''"); const curl = `curl -X ${method} ${window.location.origin}${endpoint} \\\n  -H 'Content-Type: application/json' \\\n  -d '${body}'`; setResult({ curl }, true); navigator.clipboard?.writeText(curl); setStatus('curl generated and copied.', 'ok'); }

  async function applyDefaultSchema() {
    const confirmed = window.confirm('Initialize the bundled Qornix demo database?\n\nThis will reset this generated app SQLite database, create the demo schema, and seed working sample rows for Query Builder, Table Browser and API Playground.');
    if (!confirmed) return;
    setStatus('Initializing demo database...', 'muted');
    try {
      const result = await fetchJson('/api/demo/setup', { method: 'POST' });
      setResult(result, true);
      setStatus('Demo database is ready. All bundled examples are now runnable.', 'ok');
      await refreshHomeStatus();
      await loadTables();
    } catch (error) {
      setResult(error.payload || { error: error.message }, false);
      setStatus(`Demo setup failed: ${error.message}`, 'error');
    }
  }
  async function resetDemoDatabase() { if (!window.confirm('Reset demo database? This deletes app.sqlite3, exported schema and schema history for this generated app.')) return; setStatus('Resetting demo database...', 'muted'); try { const result = await fetchJson('/api/demo/reset', { method: 'POST' }); setResult(result, true); setStatus('Demo database reset. Initialize demo database next, or use Schema Manager for manual schema apply.', 'ok'); await refreshHomeStatus(); } catch (error) { setResult(error.payload || { error: error.message }, false); setStatus(`Reset failed: ${error.message}`, 'error'); } }
  async function createDemoRows() {
    const confirmed = window.confirm('Recreate the full bundled demo dataset? This resets the demo SQLite database, applies the schema, and inserts sample rows.');
    if (!confirmed) return;
    setStatus('Reinitializing demo data...', 'muted');
    try {
      const result = await fetchJson('/api/demo/setup', { method: 'POST' });
      setResult(result, true);
      setStatus('Demo data recreated. All bundled examples are runnable.', 'ok');
      await refreshHomeStatus();
      await loadTables();
    } catch (error) {
      setResult(error.payload || { error: error.message }, false);
      setStatus(`Demo data setup failed: ${error.message}`, 'error');
    }
  }
  async function refreshHomeStatus() { const target = $('qdb-home-status'); if (!target) return; try { const tables = await fetchJson('/api/dynamic/meta/tables?limit=200&offset=0'); const names = asArray(tables); target.innerHTML = `<div class="qdb-metric-grid"><div><strong>${names.length}</strong><span>Tables</span></div><div><strong>${names.slice(0,3).map(escapeHtml).join(', ') || 'none'}</strong><span>Sample</span></div></div><p>Initialize the demo database if this shows zero tables. Then explore Query Builder, Table Browser and API Playground.</p>`; } catch (error) { target.innerHTML = `<p>Dynamic API metadata is not ready: ${escapeHtml(error.message)}</p>`; } }

  function parseRowPath() { const parts = window.location.pathname.split('/').filter(Boolean); return { table: decodeURIComponent(parts[1] || ''), id: decodeURIComponent(parts[2] || '') }; }
  function buildRowUpdatePayload() { const form = $('qdb-row-form'); const data = {}; form?.querySelectorAll('[data-row-field]').forEach((input) => { const key = input.dataset.rowField; if (key === 'id') return; data[key] = parseTypedValue(input.value); }); return { data }; }
  function renderRowForm(row) { const target = $('qdb-row-form'); if (!target) return; if (!row) { target.innerHTML = '<div class="qdb-empty">Row was not found.</div>'; return; } target.innerHTML = Object.entries(row).map(([key,value]) => `<label class="qdb-row-field"><span>${escapeHtml(key)}</span><input data-row-field="${escapeHtml(key)}" value="${escapeHtml(value)}" ${key === 'id' ? 'readonly' : ''}></label>`).join(''); target.querySelectorAll('input[data-row-field]').forEach((input) => input.addEventListener('input', () => setPreview(buildRowUpdatePayload()))); setPreview(buildRowUpdatePayload()); }
  async function loadRow() { const params = parseRowPath(); state.rowTable = params.table; state.rowId = params.id; if ($('qdb-row-title')) $('qdb-row-title').textContent = `${params.table} #${params.id}`; if ($('qdb-row-back')) $('qdb-row-back').href = `/table?table=${encodeURIComponent(params.table)}`; setStatus('Loading row...', 'muted'); try { const payload = await fetchJson(`/api/dynamic/${encodeURIComponent(params.table)}/${encodeURIComponent(params.id)}`); const row = getRowData(payload); state.row = row || {}; setResult(payload, true); renderRowForm(row); setStatus('Row loaded.', 'ok'); } catch (error) { setResult(error.payload || { error: error.message }, false); renderRowForm(null); setStatus(`Failed to load row: ${error.message}`, 'error'); } }
  async function saveRow() { const payload = buildRowUpdatePayload(); setStatus('Saving row...', 'muted'); try { const result = await fetchJson(`/api/dynamic/${encodeURIComponent(state.rowTable)}/${encodeURIComponent(state.rowId)}`, { method:'PUT', headers:{'Content-Type':'application/json'}, body:JSON.stringify(payload) }); setResult(result, true); setStatus('Row saved.', 'ok'); await loadRow(); } catch (error) { setResult(error.payload || { error: error.message }, false); setStatus(`Save failed: ${error.message}`, 'error'); } }
  async function deleteRow() { if (!window.confirm(`Delete ${state.rowTable} #${state.rowId}?`)) return; setStatus('Deleting row...', 'muted'); try { const result = await fetchJson(`/api/dynamic/${encodeURIComponent(state.rowTable)}/${encodeURIComponent(state.rowId)}`, { method:'DELETE' }); setResult(result, true); setStatus('Row deleted. Return to Table Browser.', 'ok'); } catch (error) { setResult(error.payload || { error: error.message }, false); setStatus(`Delete failed: ${error.message}`, 'error'); } }

  const recipes = {
    'metadata-tables': { title:'Metadata: tables', description:'Lists schema-backed tables visible to Dynamic API.', method:'GET', endpoint:'/api/dynamic/meta/tables?limit=100&offset=0', body:null, table:'' },
    'metadata-fields': { title:'Metadata: fields', description:'Lists readable fields for products. Useful for building UI dropdowns.', method:'GET', endpoint:'/api/dynamic/meta/products/fields?q=', body:null, table:'products' },
    'metadata-fields-info': { title:'Metadata: fields info', description:'Returns field metadata objects for a table.', method:'GET', endpoint:'/api/dynamic/meta/products/fields-info', body:null, table:'products' },
    allowlist: { title:'Metadata: allowlist', description:'Shows readable/writable/filterable/sortable field allowlist built from database schema.', method:'GET', endpoint:'/api/dynamic/meta/allowlist', body:null, table:'' },
    'list-products': { title:'List products', description:'Structured GET request sent through POST /api/dynamic.', method:'POST', endpoint:'/api/dynamic', body:{method:'GET',table:'products',query:{fields:['id','name','price','stock'],limit:25}}, table:'products' },
    'filter-products': { title:'Filter + sort products', description:'Use structured where items with op/value pairs.', method:'POST', endpoint:'/api/dynamic', body:{method:'GET',table:'products',query:{fields:['id','name','price','stock'],where:[{field:'price',op:'gte',value:40}],order_by:['price DESC'],limit:25}}, table:'products' },
    'join-orders': { title:'JOIN orders/products', description:'Uses raw JOIN clause. The first-run template enables allowRawJoin for the showcase.', method:'POST', endpoint:'/api/dynamic', body:{method:'GET',table:'orders',query:{join:['JOIN products ON orders.product_id=products.id'],filter:["status='paid'"],order_by:['total_amount DESC'],limit:10}}, table:'orders' },
    'aggregate-orders': { title:'Group + having', description:'Shows group_by and having. Having uses QueryBuilder filter syntax.', method:'POST', endpoint:'/api/dynamic', body:{method:'GET',table:'orders',query:{fields:['status'],filter:['total_amount>20'],group_by:['status'],having:['total_amount>20'],order_by:['status ASC'],limit:10}}, table:'orders' },
    'create-category': { title:'Create category', description:'Create a row via table-specific endpoint.', method:'POST', endpoint:'/api/dynamic/categories', body:{data:{name:'Generated category',description:'Created from API Playground'}}, table:'categories' },
    'create-product': { title:'Create product', description:'Create a product linked to category id 1.', method:'POST', endpoint:'/api/dynamic/products', body:{data:{category_id:1,name:'Generated product',description:'Created from API Playground',price:42.50,stock:7}}, table:'products' },
    'update-product': { title:'Update product', description:'Update a known product row by id. Initialize demo data first.', method:'PUT', endpoint:'/api/dynamic/products/1', body:{data:{name:'Wireless Mouse Pro',description:'Updated Bluetooth ergonomic mouse',price:44.50,stock:75}}, table:'products' },
    'schema-history': { title:'Schema history', description:'Read schema application audit history.', method:'GET', endpoint:'/api/dynamic/schema/history', body:null, table:'' }
  };
  function loadRecipe(name) { const recipe = recipes[name] || recipes['list-products']; state.activeRecipe = name; if ($('qdb-recipe-method')) $('qdb-recipe-method').value = recipe.method; if ($('qdb-recipe-endpoint')) $('qdb-recipe-endpoint').value = recipe.endpoint; if ($('qdb-recipe-body')) $('qdb-recipe-body').value = recipe.body ? JSON.stringify(recipe.body, null, 2) : ''; if ($('qdb-recipe-title')) $('qdb-recipe-title').textContent = recipe.title; if ($('qdb-recipe-description')) $('qdb-recipe-description').textContent = recipe.description; if ($('qdb-recipe-kicker')) $('qdb-recipe-kicker').textContent = name; document.querySelectorAll('[data-recipe]').forEach((el) => el.classList.toggle('active', el.dataset.recipe === name)); setStatus(`Loaded recipe: ${recipe.title}`, 'ok'); }
  async function runRecipe() { const method = $('qdb-recipe-method')?.value || 'GET'; const endpoint = $('qdb-recipe-endpoint')?.value || '/api/dynamic'; const bodyText = $('qdb-recipe-body')?.value.trim() || ''; const options = { method, headers:{} }; if (bodyText && method !== 'GET') { options.headers['Content-Type'] = 'application/json'; options.body = bodyText; } setStatus('Running recipe...', 'muted'); try { const result = await fetchJson(endpoint, options); setResult(result, true); const table = recipes[state.activeRecipe]?.table || (JSON.parse(bodyText || '{}').table || ''); renderResultTable(result, table); setStatus('Recipe completed.', 'ok'); } catch (error) { setResult(error.payload || error.text || { error: error.message }, false); renderResultTable(null); setStatus(`Recipe failed: ${error.message}. If this is a bundled recipe, initialize the demo database from the home page first.`, 'error'); } }
  function copyRecipeCurl() { const method = $('qdb-recipe-method')?.value || 'GET'; const endpoint = $('qdb-recipe-endpoint')?.value || '/api/dynamic'; const bodyText = $('qdb-recipe-body')?.value.trim() || ''; const bodyPart = bodyText && method !== 'GET' ? ` \\\n  -H 'Content-Type: application/json' \\\n  -d '${bodyText.replaceAll("'", "'\\''")}'` : ''; const curl = `curl -X ${method} ${window.location.origin}${endpoint}${bodyPart}`; navigator.clipboard?.writeText(curl); setResult({ curl }, true); setStatus('curl copied.', 'ok'); }

  async function loadOpenApi() { const summary = $('qdb-openapi-summary'); const pathsTarget = $('qdb-openapi-paths'); try { const spec = await fetchJson('/openapi.json'); const paths = spec.paths || {}; const pathEntries = Object.entries(paths); if (summary) summary.innerHTML = `<div class="qdb-metric-grid"><div><strong>${escapeHtml(spec.info?.title || 'OpenAPI')}</strong><span>Title</span></div><div><strong>${escapeHtml(spec.info?.version || 'n/a')}</strong><span>Version</span></div><div><strong>${pathEntries.length}</strong><span>Paths</span></div></div>`; if (pathsTarget) pathsTarget.innerHTML = pathEntries.map(([pathName, methods]) => `<article class="qdb-openapi-path">${Object.entries(methods || {}).map(([method,op]) => `<div class="qdb-openapi-method"><span>${escapeHtml(method.toUpperCase())}</span><code>${escapeHtml(pathName)}</code><p>${escapeHtml(op.summary || op.description || '')}</p></div>`).join('')}</article>`).join('') || '<div class="qdb-empty">No OpenAPI paths found.</div>'; setStatus('OpenAPI loaded.', 'ok'); } catch (error) { if (summary) summary.innerHTML = `<div class="qdb-empty">OpenAPI unavailable: ${escapeHtml(error.message)}</div>`; if (pathsTarget) pathsTarget.innerHTML = ''; setStatus(`OpenAPI load failed: ${error.message}`, 'error'); } }

  function bindCommon() { $('qdb-load-tables')?.addEventListener('click', loadTables); $('qdb-create-demo-rows')?.addEventListener('click', createDemoRows); $('qdb-add-field')?.addEventListener('click', addField); $('qdb-add-where')?.addEventListener('click', addWhere); $('qdb-add-filter')?.addEventListener('click', addFilter); $('qdb-add-join')?.addEventListener('click', addJoin); $('qdb-add-group')?.addEventListener('click', addGroup); $('qdb-add-having')?.addEventListener('click', addHaving); $('qdb-add-order')?.addEventListener('click', addOrder); $('qdb-execute')?.addEventListener('click', executeQuery); $('qdb-copy-json')?.addEventListener('click', copyPreview); $('qdb-generate-curl')?.addEventListener('click', () => generateCurl()); $('qdb-table')?.addEventListener('change', (event) => loadFields(event.target.value)); $('qdb-table-browser-table')?.addEventListener('change', (event) => loadFields(event.target.value)); $('qdb-limit')?.addEventListener('input', refreshPreview); $('qdb-table-browser-limit')?.addEventListener('input', refreshPreview); $('qdb-method')?.addEventListener('change', refreshPreview); $('qdb-data-json')?.addEventListener('input', refreshPreview); $('qdb-convert-sql')?.addEventListener('click', convertSqlToPayload); document.querySelectorAll('.qdb-example-btn').forEach((button) => button.addEventListener('click', () => loadSqlExample(button.dataset.example))); }
  function initQueryOrTable() { bindCommon(); renderAllChips(); refreshPreview(); const urlTable = new URLSearchParams(window.location.search).get('table'); if (urlTable && $('qdb-table-browser-table')) $('qdb-table-browser-table').value = urlTable; loadTables().then(() => { if (selectedTable()) loadFields(selectedTable()); }); }
  function initHome() { $('qdb-home-load-status')?.addEventListener('click', refreshHomeStatus); $('qdb-apply-default-schema')?.addEventListener('click', applyDefaultSchema); $('qdb-create-demo-rows')?.addEventListener('click', createDemoRows); $('qdb-reset-demo-db')?.addEventListener('click', resetDemoDatabase); refreshHomeStatus(); }
  function initRowView() { $('qdb-row-refresh')?.addEventListener('click', loadRow); $('qdb-row-save')?.addEventListener('click', saveRow); $('qdb-row-delete')?.addEventListener('click', deleteRow); loadRow(); }
  function initPlayground() { document.querySelectorAll('[data-recipe]').forEach((button) => button.addEventListener('click', () => loadRecipe(button.dataset.recipe))); $('qdb-run-recipe')?.addEventListener('click', runRecipe); $('qdb-copy-recipe')?.addEventListener('click', copyRecipeCurl); $('qdb-copy-recipe-json')?.addEventListener('click', copyPreview); loadRecipe('metadata-tables'); }
  function initDocs() { $('qdb-load-openapi')?.addEventListener('click', loadOpenApi); loadOpenApi(); }
  document.addEventListener('DOMContentLoaded', () => { const current = page(); if (current === 'home') initHome(); else if (current === 'query-builder' || current === 'table') initQueryOrTable(); else if (current === 'row-view') initRowView(); else if (current === 'api-playground') initPlayground(); else if (current === 'docs') initDocs(); });
})();
