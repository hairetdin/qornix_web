const api = {
  articles: "/api/wiki/articles",
  stats: "/api/wiki/stats",
  categories: "/api/wiki/categories",
  activity: "/api/wiki/activity",
  authLogin: "/api/auth/login",
  authRegister: "/api/auth/register",
  authMe: "/api/auth/me",
};

const auth = {
  token: null,
  sessionId: null,
  user: null,
};

const state = {
  articles: [],
  selected: null,
  selectedId: null,
  view: "all",
  search: "",
  category: "",
  status: "",
  editingId: null,
};

const els = {
  search: document.getElementById("searchInput"),
  filterButton: document.getElementById("filterButton"),
  filterPanel: document.getElementById("filterPanel"),
  categoryFilter: document.getElementById("categoryFilter"),
  statusFilter: document.getElementById("statusFilter"),
  resetFilters: document.getElementById("resetFiltersButton"),
  articleList: document.getElementById("articleList"),
  articleEmpty: document.getElementById("articleEmpty"),
  reader: document.getElementById("reader"),
  categoryList: document.getElementById("categoryList"),
  activityList: document.getElementById("activityList"),
  listTitle: document.getElementById("listTitle"),
  modal: document.getElementById("articleModal"),
  form: document.getElementById("articleForm"),
  modalTitle: document.getElementById("modalTitle"),
  modalSubtitle: document.getElementById("modalSubtitle"),
  deleteButton: document.getElementById("deleteArticleButton"),
  toast: document.getElementById("toast"),
  statArticles: document.getElementById("statArticles"),
  statAuthors: document.getElementById("statAuthors"),
  statFreshness: document.getElementById("statFreshness"),
  statDrafts: document.getElementById("statDrafts"),
  allCount: document.getElementById("allCount"),
  favoriteCount: document.getElementById("favoriteCount"),
  recentCount: document.getElementById("recentCount"),
  authorCount: document.getElementById("authorCount"),
};

const formFields = {
  title: document.getElementById("articleTitle"),
  category: document.getElementById("articleCategory"),
  status: document.getElementById("articleStatus"),
  author: document.getElementById("articleAuthor"),
  template: document.getElementById("articleTemplate"),
  readingTime: document.getElementById("articleReadingTime"),
  tags: document.getElementById("articleTags"),
  summary: document.getElementById("articleSummary"),
  content: document.getElementById("articleContent"),
};

function escapeHtml(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#039;");
}

function statusText(status) {
  return status === "published" ? "Published" : "Draft";
}

function statusClass(status) {
  return status === "published" ? "status-published" : "status-draft";
}

function formatDate(value) {
  if (!value) return "";
  const normalized = value.includes("T") ? value : value.replace(" ", "T") + "Z";
  const date = new Date(normalized);
  if (Number.isNaN(date.getTime())) return value;
  return new Intl.DateTimeFormat("ru-RU", {
    day: "numeric",
    month: "long",
    year: "numeric",
  }).format(date);
}

function relativeDate(value) {
  if (!value) return "";
  const normalized = value.includes("T") ? value : value.replace(" ", "T") + "Z";
  const date = new Date(normalized);
  if (Number.isNaN(date.getTime())) return value;
  const diffMs = Date.now() - date.getTime();
  const diffHours = Math.floor(diffMs / 3600000);
  if (diffHours < 1) return "только что";
  if (diffHours < 24) return `${diffHours} ч назад`;
  const diffDays = Math.floor(diffHours / 24);
  if (diffDays === 1) return "вчера";
  if (diffDays < 14) return `${diffDays} дн. назад`;
  return formatDate(value);
}

function debounce(fn, delay = 250) {
  let timer = null;
  return (...args) => {
    window.clearTimeout(timer);
    timer = window.setTimeout(() => fn(...args), delay);
  };
}

async function requestJson(url, options = {}) {
  const headers = { "Content-Type": "application/json", ...(options.headers || {}) };

  // Add auth headers if available
  if (auth.token) {
    headers["Authorization"] = "Bearer " + auth.token;
  }
  if (auth.sessionId) {
    headers["Cookie"] = "session_id=" + auth.sessionId;
  }

  const response = await fetch(url, {
    headers,
    ...options,
  });
  const payload = await response.json().catch(() => ({}));
  if (!response.ok) {
    throw new Error(payload.error || payload.message || `HTTP ${response.status}`);
  }
  return payload;
}

// ---------------------------------------------------------------------------
// Auth functions
// ---------------------------------------------------------------------------

function saveAuthState() {
  try {
    localStorage.setItem("wiki_auth", JSON.stringify({
      token: auth.token,
      sessionId: auth.sessionId,
      user: auth.user,
    }));
  } catch (_) { /* ignore */ }
}

function loadAuthState() {
  try {
    const raw = localStorage.getItem("wiki_auth");
    if (raw) {
      const parsed = JSON.parse(raw);
      auth.token = parsed.token || null;
      auth.sessionId = parsed.sessionId || null;
      auth.user = parsed.user || null;
    }
  } catch (_) { /* ignore */ }
}

async function authLogin(username, password) {
  const response = await fetch(api.authLogin, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ username, password }),
  });
  const payload = await response.json().catch(() => ({}));
  if (!response.ok) {
    throw new Error(payload.error || payload.message || "Ошибка входа");
  }
  auth.token = payload.jwtToken || null;
  auth.sessionId = payload.sessionId || null;
  auth.user = {
    id: payload.userId,
    username: payload.username,
    email: payload.email,
  };
  saveAuthState();
  return auth.user;
}

async function authRegister(username, email, password) {
  const response = await fetch(api.authRegister, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ username, email, password }),
  });
  const payload = await response.json().catch(() => ({}));
  if (!response.ok) {
    throw new Error(payload.error || payload.message || "Ошибка регистрации");
  }
  return payload;
}

async function authCheckMe() {
  try {
    const user = await requestJson(api.authMe);
    auth.user = user;
    saveAuthState();
    return user;
  } catch (_) {
    authLogout();
    return null;
  }
}

function authLogout() {
  auth.token = null;
  auth.sessionId = null;
  auth.user = null;
  try { localStorage.removeItem("wiki_auth"); } catch (_) { /* ignore */ }
  updateAuthUI();
}

function updateAuthUI() {
  const authArea = document.getElementById("authArea");
  const createButton = document.getElementById("createButton");
  const sidebarCreateButton = document.getElementById("sidebarCreateButton");

  if (auth.user) {
    authArea.innerHTML = `
      <div class="auth-user-info">
        <span class="username">${escapeHtml(auth.user.username)}</span>
        <button class="ghost-btn compact logout-btn" id="logoutButton" type="button">Выйти</button>
      </div>
    `;
    const logoutBtn = document.getElementById("logoutButton");
    if (logoutBtn) {
      logoutBtn.addEventListener("click", () => {
        authLogout();
        showToast("Вы вышли из аккаунта");
      });
    }
  } else {
    authArea.innerHTML = `<button class="ghost-btn" id="loginButton" type="button">Войти</button>`;
    const loginBtn = document.getElementById("loginButton");
    if (loginBtn) {
      loginBtn.addEventListener("click", openLoginModal);
    }
  }

  // Show/hide create buttons based on auth state
  const isAuth = !!auth.user;
  if (createButton) createButton.style.display = isAuth ? "" : "none";
  if (sidebarCreateButton) sidebarCreateButton.parentElement.style.display = isAuth ? "" : "none";
}

function openLoginModal() {
  const modal = document.getElementById("loginModal");
  if (modal) modal.hidden = false;
  const usernameInput = document.getElementById("loginUsername");
  if (usernameInput) usernameInput.focus();
}

function closeLoginModal() {
  const modal = document.getElementById("loginModal");
  if (modal) modal.hidden = true;
  const form = document.getElementById("loginForm");
  if (form) form.reset();
}

function openRegisterModal() {
  const loginModal = document.getElementById("loginModal");
  if (loginModal) loginModal.hidden = true;
  const modal = document.getElementById("registerModal");
  if (modal) modal.hidden = false;
  const usernameInput = document.getElementById("registerUsername");
  if (usernameInput) usernameInput.focus();
}

function closeRegisterModal() {
  const modal = document.getElementById("registerModal");
  if (modal) modal.hidden = true;
  const form = document.getElementById("registerForm");
  if (form) form.reset();
}

function buildArticleQuery() {
  const params = new URLSearchParams();
  if (state.search) params.set("q", state.search);
  if (state.category) params.set("category", state.category);
  if (state.status) params.set("status", state.status);
  if (state.view === "favorites") params.set("favorite", "1");
  if (state.view === "recent") params.set("recent", "1");
  const query = params.toString();
  return query ? `${api.articles}?${query}` : api.articles;
}

async function loadStats() {
  const stats = await requestJson(api.stats);
  els.statArticles.textContent = stats.articles ?? 0;
  els.statAuthors.textContent = stats.authors ?? 0;
  els.statFreshness.textContent = `${stats.freshness ?? 0}%`;
  els.statDrafts.textContent = stats.drafts ?? 0;
  els.allCount.textContent = stats.articles ?? 0;
  els.favoriteCount.textContent = stats.favorites ?? 0;
  els.recentCount.textContent = stats.recent ?? 0;
  els.authorCount.textContent = stats.authors ?? 0;
}

async function loadCategories() {
  const payload = await requestJson(api.categories);
  const categories = payload.categories || [];
  els.categoryList.innerHTML = categories.map((category) => `
    <li>
      <button class="space-item ${state.category === category.name ? "active" : ""}" data-category="${escapeHtml(category.name)}">
        <span>${escapeHtml(category.name)}</span>
        <span class="count">${category.count}</span>
      </button>
    </li>
  `).join("");

  const current = state.category;
  els.categoryFilter.innerHTML = `<option value="">Все категории</option>` + categories.map((category) => `
    <option value="${escapeHtml(category.name)}">${escapeHtml(category.name)}</option>
  `).join("");
  els.categoryFilter.value = current;
}

async function loadActivity() {
  const payload = await requestJson(api.activity);
  const activity = payload.activity || [];
  els.activityList.innerHTML = activity.map((item) => `
    <div class="mini-item">
      <div class="mini-meta">
        <span>${escapeHtml(relativeDate(item.createdAt))}</span>
        <span>${escapeHtml(item.actor)}</span>
      </div>
      <p>${escapeHtml(item.action)}: ${escapeHtml(item.detail)}</p>
    </div>
  `).join("");
}

async function loadArticles({ keepSelection = true } = {}) {
  const payload = await requestJson(buildArticleQuery());
  state.articles = payload.articles || [];
  renderArticleList();

  if (!state.articles.length) {
    state.selected = null;
    state.selectedId = null;
    renderReader();
    return;
  }

  const hashId = Number((location.hash.match(/article-(\d+)/) || [])[1]);
  const preferredId = keepSelection ? (state.selectedId || hashId) : hashId;
  const next = state.articles.find((article) => article.id === preferredId) || state.articles[0];
  await selectArticle(next.id);
}

function renderArticleList() {
  const titleByView = {
    all: "Популярные статьи",
    favorites: "Избранные статьи",
    recent: "Недавние обновления",
    team: "Командная база знаний",
  };
  els.listTitle.textContent = titleByView[state.view] || "Статьи";
  els.articleEmpty.hidden = state.articles.length > 0;
  els.articleList.innerHTML = state.articles.map((article) => `
    <button class="article-card ${article.id === state.selectedId ? "active" : ""}" type="button" data-id="${article.id}">
      <div class="article-meta">
        <span>Обновлено ${escapeHtml(relativeDate(article.updatedAt))}</span>
        <span class="status-badge ${statusClass(article.status)}">${statusText(article.status)}</span>
      </div>
      <h4>${escapeHtml(article.title)}</h4>
      <p>${escapeHtml(article.summary)}</p>
      <div class="tag-row">
        ${(article.tags || []).slice(0, 3).map((tag) => `<span class="tag">${escapeHtml(tag)}</span>`).join("")}
      </div>
      <div class="article-card-actions">
        <button class="link-btn compact open-detail-btn" type="button" data-detail-id="${article.id}" title="Открыть статью">🔗 Детальный просмотр</button>
      </div>
    </button>
  `).join("");
}

async function selectArticle(id) {
  const article = await requestJson(`${api.articles}/${id}`);
  state.selected = article;
  state.selectedId = article.id;
  location.hash = `article-${article.id}`;
  renderArticleList();
  renderReader();
}

function renderContent(content) {
  const lines = String(content || "").split(/\r?\n/);
  const blocks = [];
  let listItems = [];

  const flushList = () => {
    if (!listItems.length) return;
    blocks.push(`<ul>${listItems.map((item) => `<li>${escapeHtml(item)}</li>`).join("")}</ul>`);
    listItems = [];
  };

  for (const rawLine of lines) {
    const line = rawLine.trim();
    if (!line) {
      flushList();
      continue;
    }
    if (line.startsWith("- ")) {
      listItems.push(line.slice(2));
      continue;
    }
    flushList();
    if (line.startsWith("## ")) {
      blocks.push(`<h3>${escapeHtml(line.slice(3))}</h3>`);
    } else {
      blocks.push(`<p>${escapeHtml(line)}</p>`);
    }
  }
  flushList();
  return blocks.join("");
}

function renderReader() {
  const article = state.selected;
  if (!article) {
    els.reader.innerHTML = `
      <div class="empty-state">Выберите статью из списка или создайте новую</div>
    `;
    return;
  }

  els.reader.innerHTML = `
    <div class="reader-header">
      <div>
        <h2>${escapeHtml(article.title)}</h2>
        <div class="reader-subtitle">
          <span>Автор: ${escapeHtml(article.author)}</span>
          <span>Обновлено: ${escapeHtml(formatDate(article.updatedAt))}</span>
          <span>Время чтения: ${article.readingTime} мин</span>
          <span>Просмотры: ${article.views}</span>
        </div>
        <div class="tag-row">
          ${(article.tags || []).map((tag) => `<span class="tag">${escapeHtml(tag)}</span>`).join("")}
        </div>
      </div>
      <div class="reader-actions">
        <button class="ghost-btn" type="button" data-action="favorite">${article.favorite ? "В избранном" : "В избранное"}</button>
        <button class="ghost-btn" type="button" data-action="share">Поделиться</button>
        <button class="primary-btn" type="button" data-action="edit">Редактировать</button>
      </div>
    </div>

    <div class="reader-section">
      <h3>Описание</h3>
      <p>${escapeHtml(article.summary)}</p>
    </div>

    <div class="info-grid">
      <div class="info-card">
        <span>Категория</span>
        <strong>${escapeHtml(article.category)}</strong>
      </div>
      <div class="info-card">
        <span>Статус</span>
        <strong>${article.status === "published" ? "Актуально" : "Черновик"}</strong>
      </div>
      <div class="info-card">
        <span>Шаблон</span>
        <strong>${escapeHtml(article.template)}</strong>
      </div>
    </div>

    <div class="reader-section">
      ${renderContent(article.content)}
    </div>
  `;
}

function openModal(article = null, templateName = "") {
  state.editingId = article ? article.id : null;
  els.modalTitle.textContent = article ? "Редактировать статью" : "Новая статья";
  els.modalSubtitle.textContent = article ? `ID ${article.id} в SQLite` : "Материал будет сохранен в SQLite";
  els.deleteButton.hidden = !article;

  formFields.title.value = article?.title || "";
  formFields.category.value = article?.category || state.category || "General";
  formFields.status.value = article?.status || "published";
  formFields.author.value = article?.author || "Wiki Team";
  formFields.template.value = article?.template || templateName || "Guide";
  formFields.readingTime.value = article?.readingTime || 4;
  formFields.tags.value = (article?.tags || []).join(", ");
  formFields.summary.value = article?.summary || "";
  formFields.content.value = article?.content || templateContent(templateName || "Guide");

  els.modal.hidden = false;
  formFields.title.focus();
}

function closeModal() {
  els.modal.hidden = true;
  els.form.reset();
  state.editingId = null;
}

function templateContent(templateName) {
  if (templateName === "Checklist") {
    return "## Перед началом\n- Определить владельца.\n- Проверить входные данные.\n- Зафиксировать критерии готовности.\n\n## Выполнение\n- Выполнить основной шаг.\n- Проверить результат.\n- Обновить связанные материалы.";
  }
  if (templateName === "Reference") {
    return "## Назначение\nОпишите, где применяется справочник и кто отвечает за актуальность.\n\n## Правила\n- Первое правило.\n- Второе правило.\n\n## Исключения\nОпишите редкие случаи и ограничения.";
  }
  return "## Контекст\nОпишите, когда и кому нужна эта инструкция.\n\n## Шаги\n- Первый шаг.\n- Второй шаг.\n- Проверка результата.\n\n## Полезные ссылки\nДобавьте связанные материалы.";
}

function articlePayload() {
  return {
    title: formFields.title.value.trim(),
    category: formFields.category.value.trim(),
    status: formFields.status.value,
    author: formFields.author.value.trim(),
    template: formFields.template.value,
    readingTime: Number(formFields.readingTime.value || 4),
    tags: formFields.tags.value.split(",").map((tag) => tag.trim()).filter(Boolean),
    summary: formFields.summary.value.trim(),
    content: formFields.content.value.trim(),
  };
}

async function saveArticle(event) {
  event.preventDefault();
  const payload = articlePayload();
  const url = state.editingId ? `${api.articles}/${state.editingId}` : api.articles;
  const method = state.editingId ? "PUT" : "POST";
  const article = await requestJson(url, {
    method,
    body: JSON.stringify(payload),
  });
  closeModal();
  state.selectedId = article.id;
  await refreshAll();
  showToast("Статья сохранена");
}

async function deleteSelectedArticle() {
  if (!state.editingId) return;
  const confirmed = window.confirm("Удалить статью без восстановления?");
  if (!confirmed) return;
  await requestJson(`${api.articles}/${state.editingId}`, { method: "DELETE" });
  closeModal();
  state.selectedId = null;
  await refreshAll({ keepSelection: false });
  showToast("Статья удалена");
}

async function toggleFavorite() {
  if (!state.selectedId) return;
  const article = await requestJson(`${api.articles}/${state.selectedId}/favorite`, { method: "PATCH" });
  state.selected = article;
  await refreshAll();
  showToast(article.favorite ? "Добавлено в избранное" : "Убрано из избранного");
}

async function shareSelected() {
  if (!state.selectedId) return;
  const url = `${location.origin}${location.pathname}#article-${state.selectedId}`;
  await navigator.clipboard?.writeText(url);
  showToast("Ссылка скопирована");
}

function showToast(message) {
  els.toast.textContent = message;
  els.toast.hidden = false;
  window.clearTimeout(showToast.timer);
  showToast.timer = window.setTimeout(() => {
    els.toast.hidden = true;
  }, 2600);
}

async function refreshAll(options = {}) {
  await Promise.all([loadStats(), loadCategories(), loadActivity()]);
  await loadArticles(options);
}

function bindEvents() {
  els.search.addEventListener("input", debounce(() => {
    state.search = els.search.value.trim();
    loadArticles({ keepSelection: false }).catch((error) => showToast(error.message));
  }));

  els.filterButton.addEventListener("click", () => {
    els.filterPanel.hidden = !els.filterPanel.hidden;
  });

  els.categoryFilter.addEventListener("change", () => {
    state.category = els.categoryFilter.value;
    refreshAll({ keepSelection: false }).catch((error) => showToast(error.message));
  });

  els.statusFilter.addEventListener("change", () => {
    state.status = els.statusFilter.value;
    loadArticles({ keepSelection: false }).catch((error) => showToast(error.message));
  });

  els.resetFilters.addEventListener("click", () => {
    state.search = "";
    state.category = "";
    state.status = "";
    els.search.value = "";
    els.statusFilter.value = "";
    refreshAll({ keepSelection: false }).catch((error) => showToast(error.message));
  });

  document.querySelectorAll("[data-view]").forEach((button) => {
    button.addEventListener("click", () => {
      document.querySelectorAll("[data-view]").forEach((item) => item.classList.remove("active"));
      button.classList.add("active");
      state.view = button.dataset.view;
      loadArticles({ keepSelection: false }).catch((error) => showToast(error.message));
    });
  });

  els.categoryList.addEventListener("click", (event) => {
    const button = event.target.closest("[data-category]");
    if (!button) return;
    state.category = button.dataset.category;
    els.categoryFilter.value = state.category;
    refreshAll({ keepSelection: false }).catch((error) => showToast(error.message));
  });

  els.articleList.addEventListener("click", (event) => {
    // Handle detail page open button
    const detailBtn = event.target.closest(".open-detail-btn");
    if (detailBtn) {
      event.stopPropagation();
      const detailUrl = `${location.pathname}article/${detailBtn.dataset.detailId}`;
      window.open(detailUrl, "_blank");
      return;
    }

    // Handle article card click (select and show in reader)
    const button = event.target.closest("[data-id]");
    if (!button) return;
    selectArticle(Number(button.dataset.id)).catch((error) => showToast(error.message));
  });

  els.reader.addEventListener("click", (event) => {
    const button = event.target.closest("[data-action]");
    if (!button) return;
    if (button.dataset.action === "edit") openModal(state.selected);
    if (button.dataset.action === "favorite") toggleFavorite().catch((error) => showToast(error.message));
    if (button.dataset.action === "share") shareSelected().catch((error) => showToast(error.message));
  });

  document.getElementById("createButton").addEventListener("click", () => openModal());
  document.getElementById("sidebarCreateButton").addEventListener("click", () => openModal());
  document.getElementById("templateCreateButton").addEventListener("click", () => openModal(null, "Guide"));
  document.getElementById("refreshButton").addEventListener("click", () => refreshAll().catch((error) => showToast(error.message)));
  document.getElementById("activityRefreshButton").addEventListener("click", () => loadActivity().catch((error) => showToast(error.message)));
  document.getElementById("closeModalButton").addEventListener("click", closeModal);
  document.getElementById("cancelModalButton").addEventListener("click", closeModal);
  els.deleteButton.addEventListener("click", () => deleteSelectedArticle().catch((error) => showToast(error.message)));
  els.form.addEventListener("submit", (event) => saveArticle(event).catch((error) => showToast(error.message)));

  document.querySelectorAll("[data-template]").forEach((button) => {
    button.addEventListener("click", () => openModal(null, button.dataset.template));
  });

  els.modal.addEventListener("click", (event) => {
    if (event.target === els.modal) closeModal();
  });

  // Auth modal events
  document.getElementById("closeLoginModalButton")?.addEventListener("click", closeLoginModal);
  document.getElementById("cancelLoginModalButton")?.addEventListener("click", closeLoginModal);
  document.getElementById("closeRegisterModalButton")?.addEventListener("click", closeRegisterModal);
  document.getElementById("cancelRegisterModalButton")?.addEventListener("click", closeRegisterModal);
  document.getElementById("showRegisterButton")?.addEventListener("click", openRegisterModal);
  document.getElementById("showLoginButton")?.addEventListener("click", openLoginModal);

  // Login form
  const loginForm = document.getElementById("loginForm");
  loginForm?.addEventListener("submit", async (event) => {
    event.preventDefault();
    const username = document.getElementById("loginUsername").value.trim();
    const password = document.getElementById("loginPassword").value;
    try {
      await authLogin(username, password);
      closeLoginModal();
      updateAuthUI();
      showToast(`Добро пожаловать, ${auth.user.username}!`);
      await refreshAll();
    } catch (error) {
      showToast(error.message || "Ошибка входа");
    }
  });

  // Register form
  const registerForm = document.getElementById("registerForm");
  registerForm?.addEventListener("submit", async (event) => {
    event.preventDefault();
    const username = document.getElementById("registerUsername").value.trim();
    const email = document.getElementById("registerEmail").value.trim();
    const password = document.getElementById("registerPassword").value;
    try {
      await authRegister(username, email, password);
      closeRegisterModal();
      showToast("Регистрация успешна! Теперь войдите в аккаунт");
    } catch (error) {
      showToast(error.message || "Ошибка регистрации");
    }
  });
}

// Initialize auth state on page load
bindEvents();
loadAuthState();
updateAuthUI();

// Check auth status, then load wiki data
(async () => {
  await authCheckMe();
  refreshAll({ keepSelection: false }).catch((error) => showToast(error.message));
})();
