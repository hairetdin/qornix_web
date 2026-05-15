<template>
  <div class="app-shell">
    <header class="topbar">
      <RouterLink class="brand" to="/">
        <span class="brand-mark">Qx</span>
        <span>
          <strong>Qornix Dynamic Vue</strong>
          <small>Backend API + Vue scaffold</small>
        </span>
      </RouterLink>
      <nav class="nav-links" aria-label="Main navigation">
        <RouterLink to="/">Home</RouterLink>
        <RouterLink to="/backend-admin">Backend Admin</RouterLink>
        <a href="/project-structure">Project Structure</a>
        <a href="/api/dynamic/openapi.json" target="_blank" rel="noreferrer">OpenAPI</a>
      </nav>
    </header>

    <main>
      <RouterView />
    </main>
  </div>
</template>
