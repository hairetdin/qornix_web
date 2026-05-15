<script setup lang="ts">
const readyItems = [
  { label: 'Vue frontend', value: 'served from /' },
  { label: 'Dynamic API', value: '/api/dynamic/*' },
  { label: 'Backend tools', value: '/backend/*' },
  { label: 'C++ extension points', value: 'handlers/ + routes.h' },
  { label: 'Production build', value: 'bundled by CMake' }
]

const platformCards = [
  {
    title: 'Backend is already alive',
    text: 'The generated C++ application starts with Dynamic API routes, metadata endpoints, OpenAPI and backend inspection tools already wired together.',
    href: '/api/dynamic/meta/tables',
    action: 'Inspect metadata'
  },
  {
    title: 'Vue is ready for product work',
    text: 'Routing, Vite, TypeScript, dev proxy and production build are prepared. Replace these starter pages with your real product screens.',
    href: '/backend-admin',
    action: 'Open developer hub'
  },
  {
    title: 'One build, one runtime bundle',
    text: 'CMake builds the backend, runs the frontend production build and places the compiled SPA into the deploy package.',
    href: '/backend/docs',
    action: 'Read backend docs'
  },
  {
    title: 'Extend backend with fast C++ code',
    text: 'Keep the generated Dynamic API for common data access and add your own handlers, routes and business operations in the C++ backend when the product needs custom logic.',
    href: 'https://github.com/hairetdin/qornix_web',
    action: 'Open Qornix Web'
  }
]

const workflow = [
  'Describe or update your data model with the backend Schema Manager.',
  'Explore generated endpoints through the API Playground and OpenAPI document.',
  'Build Vue pages, components and business flows in the frontend workspace.',
  'Add custom C++ handlers and routes when the Dynamic API is not enough.',
  'Ship the deploy bundle as a complete backend + frontend application.'
]

const resourceLinks = [
  {
    title: 'Qornix Web framework',
    href: 'https://github.com/hairetdin/qornix_web',
    description: 'Source framework for this generated application. Use it to study routing, handlers, templates, examples and framework-level capabilities.'
  },
  {
    title: 'Vue documentation',
    href: 'https://vuejs.org/guide/introduction.html',
    description: 'Core Vue guide for components, reactivity, templates and application architecture.'
  },
  {
    title: 'Vue Router',
    href: 'https://router.vuejs.org/guide/',
    description: 'Routing guide for adding product pages, nested screens and navigation flows.'
  },
  {
    title: 'Vite guide',
    href: 'https://vite.dev/guide/',
    description: 'Development server, production build, plugins and frontend tooling documentation.'
  }
]
</script>

<template>
  <section class="hero page-grid">
    <div class="hero-copy gradient-panel">
      <p class="eyebrow">Fullstack scaffold is ready</p>
      <h1>Build your product, not the plumbing.</h1>
      <p class="lead">
        Qornix generated a working C++ backend, connected Dynamic API, backend administration tools
        and a Vue frontend workspace. The foundation is already prepared, but it is not locked down:
        extend the backend with fast custom C++ code whenever your product needs extra logic.
      </p>
      <div class="actions">
        <RouterLink class="button primary" to="/backend-admin">Open developer hub</RouterLink>
        <a class="button" href="/backend/api-playground">Try API Playground</a>
        <a class="button ghost" href="/api/dynamic/openapi.json" target="_blank" rel="noreferrer">View OpenAPI</a>
      </div>
    </div>

    <aside class="status-card launch-card">
      <span class="badge">Environment online</span>
      <h2>Your starter platform includes</h2>
      <div class="status-list">
        <div v-for="item in readyItems" :key="item.label" class="status-row">
          <span class="status-dot" aria-hidden="true"></span>
          <strong>{{ item.label }}</strong>
          <code>{{ item.value }}</code>
        </div>
      </div>
      <p>
        Keep this page as a welcome screen, or replace it with your own dashboard when the frontend evolves.
      </p>
    </aside>
  </section>

  <section class="section-block">
    <div class="section-title">
      <div>
        <p class="eyebrow">What you get immediately</p>
        <h2>A prepared application environment</h2>
      </div>
      <p>
        The template is intentionally practical: it gives frontend developers a running backend contract
        and gives backend developers familiar debug tools under <code>/backend</code>, plus a clear place
        to add custom C++ routes and product-specific operations.
      </p>
    </div>

    <div class="cards four-columns">
      <article v-for="card in platformCards" :key="card.title" class="card feature-card">
        <h3>{{ card.title }}</h3>
        <p>{{ card.text }}</p>
        <a :href="card.href">{{ card.action }}</a>
      </article>
    </div>
  </section>


  <section class="section-block split-panel">
    <article class="card emphasis-card">
      <p class="eyebrow">Backend extension</p>
      <h2>Use Dynamic API first. Add C++ when the product needs more.</h2>
      <p>
        The generated backend is a normal Qornix Web application. You can keep the automatic CRUD/query API
        for data screens, then add your own C++ handlers for payments, reports, integrations, background
        operations, validation rules or any other domain-specific workflow.
      </p>
      <div class="inline-code-list">
        <code>backend/handlers/</code>
        <code>backend/routes.h</code>
        <code>/api/operations/*</code>
      </div>
    </article>

    <article class="card">
      <p class="eyebrow">Useful documentation</p>
      <h2>Open the source and frontend guides</h2>
      <div class="resource-list">
        <a v-for="link in resourceLinks" :key="link.href" :href="link.href" target="_blank" rel="noreferrer">
          <strong>{{ link.title }}</strong>
          <span>{{ link.description }}</span>
        </a>
      </div>
    </article>
  </section>

  <section class="section-block split-panel">
    <article class="card emphasis-card">
      <p class="eyebrow">Recommended next steps</p>
      <h2>Start from the ready contract</h2>
      <ol class="timeline-list">
        <li v-for="item in workflow" :key="item">{{ item }}</li>
      </ol>
    </article>

    <article class="card code-card">
      <p class="eyebrow">Frontend workspace</p>
      <h2>Develop as a normal Vue app</h2>
      <p>
        Use the Vite dev server for fast UI iteration. Requests to <code>/api</code>, <code>/backend</code>,
        <code>/static</code> and <code>/health</code> are proxied to the running backend.
      </p>
      <pre><code>cd frontend
npm install
npm run dev</code></pre>
    </article>
  </section>

  <section class="card final-callout">
    <div>
      <p class="eyebrow">Future-ready</p>
      <h2>Use it as a clean Vue project today. Add a constructor later only if you need one.</h2>
      <p>
        This MVP does not force XML-driven UI generation on every project. It provides the stable foundation:
        backend API, Vue shell, build pipeline and useful administration tools.
      </p>
    </div>
    <RouterLink class="button primary" to="/backend-admin">See available tools</RouterLink>
  </section>
</template>
