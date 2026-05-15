<script setup lang="ts">
type LinkItem = {
  title: string
  href: string
  description: string
  tag: string
}

const backendLinks: LinkItem[] = [
  {
    title: 'Backend Home',
    href: '/backend',
    tag: 'Overview',
    description: 'Original Dynamic API landing page with entry points to backend tools and demo workflow.'
  },
  {
    title: 'Schema Manager',
    href: '/backend/schema-manager',
    tag: 'Schema',
    description: 'Validate XML, preview generated SQL, build a migration plan and apply schema changes safely.'
  },
  {
    title: 'Query Builder',
    href: '/backend/query-builder',
    tag: 'Queries',
    description: 'Compose Dynamic API query parameters visually before wiring them into frontend screens.'
  },
  {
    title: 'Table Browser',
    href: '/backend/table',
    tag: 'Data',
    description: 'Inspect available tables, rows, filters and record details from the generated backend UI.'
  },
  {
    title: 'API Playground',
    href: '/backend/api-playground',
    tag: 'Testing',
    description: 'Try generated backend requests without leaving the application or opening external tools.'
  },
  {
    title: 'Documentation',
    href: '/backend/docs',
    tag: 'Docs',
    description: 'Open the documentation hub for Dynamic API usage, schema guides and backend references.'
  },
  {
    title: 'Query Syntax',
    href: '/backend/docs/query-syntax',
    tag: 'Docs',
    description: 'Learn the filter, sorting, pagination and relation syntax exposed by the Dynamic API.'
  },
  {
    title: 'Schema Docs',
    href: '/backend/docs/schema',
    tag: 'Docs',
    description: 'Review XML schema conventions and Schema Manager workflow details.'
  },
  {
    title: 'Metadata: tables',
    href: '/api/dynamic/meta/tables',
    tag: 'JSON',
    description: 'Use this endpoint to discover tables from frontend code or during manual inspection.'
  },
  {
    title: 'OpenAPI JSON',
    href: '/api/dynamic/openapi.json',
    tag: 'Contract',
    description: 'Generate TypeScript types, API clients or documentation from the live backend contract.'
  },
  {
    title: 'Health Check',
    href: '/health',
    tag: 'Runtime',
    description: 'Minimal endpoint for smoke tests, reverse proxies, containers and deployment checks.'
  }
]

const capabilities = [
  'Generated Dynamic CRUD/query API under /api/dynamic/*',
  'Metadata endpoints for frontend discovery and debugging',
  'Custom C++ backend handlers and routes for product-specific logic',
  'Schema Manager for XML validation, SQL preview and apply workflow',
  'Query Builder and Table Browser for manual API exploration',
  'OpenAPI document for generated frontend types and SDKs',
  'Qornix Web framework foundation with reusable routing and handler patterns',
  'Vue + Vite + TypeScript scaffold served as the application homepage',
  'Vite development proxy to the running backend',
  'CMake-integrated frontend production build and deploy bundle copy',
  'Legacy backend pages preserved under /backend/* instead of occupying /'
]

const projectAreas = [
  {
    name: 'backend/',
    description: 'C++ application, routes, config, schema files, backend templates and static debug assets. Add custom handlers here when Dynamic API is not enough.'
  },
  {
    name: 'frontend/',
    description: 'Vue application source. This is the main workspace for product UI development.'
  },
  {
    name: 'build/deploy/@PROJECT_NAME@/',
    description: 'Portable runtime bundle with compiled backend, backend assets and production frontend build.'
  }
]

const cppExtensionPoints = [
  {
    title: 'Handlers',
    path: 'backend/handlers/',
    description: 'Put reusable C++ request handlers, business operations and integrations here.'
  },
  {
    title: 'Routes',
    path: 'backend/routes.h',
    description: 'Register new HTTP endpoints next to the prepared Dynamic API and backend tool routes.'
  },
  {
    title: 'Operations API',
    path: '/api/operations/*',
    description: 'Recommended namespace for custom product actions that do more than generic CRUD.'
  }
]

const documentationLinks: LinkItem[] = [
  {
    title: 'Qornix Web on GitHub',
    href: 'https://github.com/hairetdin/qornix_web',
    tag: 'Framework',
    description: 'Source framework behind this generated application: routing, handlers, examples and core backend capabilities.'
  },
  {
    title: 'Vue Guide',
    href: 'https://vuejs.org/guide/introduction.html',
    tag: 'Frontend',
    description: 'Official Vue documentation for components, reactivity, templates and application structure.'
  },
  {
    title: 'Vue Router Guide',
    href: 'https://router.vuejs.org/guide/',
    tag: 'Routing',
    description: 'Use it when adding new frontend pages, nested routes and navigation behavior.'
  },
  {
    title: 'Vite Guide',
    href: 'https://vite.dev/guide/',
    tag: 'Tooling',
    description: 'Frontend dev server, build pipeline, plugins, proxy configuration and production build behavior.'
  },
  {
    title: 'Vite Vue Plugin',
    href: 'https://vite.dev/plugins/',
    tag: 'Tooling',
    description: 'Official plugin entry point for Vue single-file component support in Vite projects.'
  },
  {
    title: 'Pinia',
    href: 'https://pinia.vuejs.org/',
    tag: 'State',
    description: 'Optional state management library for larger Vue applications.'
  }
]
</script>

<template>
  <section class="admin-hero">
    <div>
      <p class="eyebrow">Developer hub</p>
      <h1>Backend Admin & Project Guide</h1>
      <p class="lead">
        Everything needed to start frontend development is already running: backend API, documentation,
        schema tools, table inspection, OpenAPI and a Vue workspace connected to the same runtime.
        When generated CRUD is not enough, the same backend can be extended with fast custom C++ code.
      </p>
      <div class="actions">
        <a class="button primary" href="/backend/schema-manager">Open Schema Manager</a>
        <a class="button" href="/backend/api-playground">Open API Playground</a>
        <a class="button ghost" href="/api/dynamic/openapi.json" target="_blank" rel="noreferrer">OpenAPI JSON</a>
      </div>
    </div>
  </section>

  <section class="section-block process-card card">
    <p class="eyebrow">How the environment fits together</p>
    <div class="process-flow" aria-label="Development workflow">
      <span>Schema</span>
      <i aria-hidden="true">→</i>
      <span>Dynamic API</span>
      <i aria-hidden="true">→</i>
      <span>Vue frontend</span>
      <i aria-hidden="true">→</i>
      <span>Business flows</span>
      <i aria-hidden="true">→</i>
      <span>Deploy bundle</span>
    </div>
    <p>
      Use backend tools to understand the generated contract, then build real screens in Vue. Custom C++ handlers
      can be added for operations that go beyond generic CRUD/query behavior: reports, imports, integrations,
      validation, background jobs or any product-specific business flow.
    </p>
  </section>

  <section class="cards two-columns quickstart-grid">
    <article class="card">
      <p class="eyebrow">Run the backend bundle</p>
      <h2>Production-like runtime</h2>
      <pre><code>mkdir -p build
cd build
cmake ..
cmake --build .
cd deploy/@PROJECT_NAME@
./@PROJECT_NAME@</code></pre>
      <p>
        The CMake build also prepares the frontend production assets that are served by the backend runtime.
      </p>
    </article>

    <article class="card">
      <p class="eyebrow">Work on the frontend</p>
      <h2>Fast Vue development</h2>
      <pre><code>cd frontend
npm install
npm run dev</code></pre>
      <p>
        The dev server keeps frontend iteration fast while proxying API and backend-tool requests to
        <code>http://127.0.0.1:8008</code>.
      </p>
    </article>
  </section>


  <section class="cards two-columns section-block">
    <article class="card">
      <p class="eyebrow">Backend extensibility</p>
      <h2>Add your own fast C++ backend logic</h2>
      <p>
        The template is generated on top of Qornix Web. It is not a black box: keep the Dynamic API for
        standard data access, and add dedicated C++ endpoints when you need custom behavior or maximum control.
      </p>
      <div class="project-map compact-map">
        <div v-for="point in cppExtensionPoints" :key="point.path" class="project-row">
          <strong>{{ point.title }}</strong>
          <code>{{ point.path }}</code>
          <p>{{ point.description }}</p>
        </div>
      </div>
    </article>

    <article class="card">
      <p class="eyebrow">Recommended pattern</p>
      <h2>Use the right layer for each task</h2>
      <ul class="check-list spacious-list">
        <li>Use <code>/api/dynamic/*</code> for generated CRUD, metadata and query-driven screens.</li>
        <li>Use Vue routes and components for product UI, dashboards and workflows.</li>
        <li>Use custom C++ handlers for domain operations, integrations and performance-sensitive logic.</li>
        <li>Use OpenAPI and backend docs as the frontend/backend contract.</li>
      </ul>
    </article>
  </section>

  <section class="card links-card section-block">
    <div class="section-title">
      <div>
        <p class="eyebrow">Backend toolbelt</p>
        <h2>Open the prepared tools</h2>
      </div>
      <p>
        These links are available immediately after the generated application starts. They help frontend
        and backend developers inspect the live contract before writing product code.
      </p>
    </div>
    <div class="link-grid">
      <a v-for="item in backendLinks" :key="item.href" class="link-tile" :href="item.href" target="_blank" rel="noreferrer">
        <span class="tile-tag">{{ item.tag }}</span>
        <strong>{{ item.title }}</strong>
        <code>{{ item.href }}</code>
        <span>{{ item.description }}</span>
      </a>
    </div>
  </section>


  <section class="card links-card section-block">
    <div class="section-title">
      <div>
        <p class="eyebrow">External documentation</p>
        <h2>Framework and frontend references</h2>
      </div>
      <p>
        Use these links when moving from the generated starter to real product development. Qornix Web is
        the backend foundation; Vue, Vue Router and Vite are the frontend stack prepared by this template.
      </p>
    </div>
    <div class="link-grid">
      <a v-for="item in documentationLinks" :key="item.href" class="link-tile" :href="item.href" target="_blank" rel="noreferrer">
        <span class="tile-tag">{{ item.tag }}</span>
        <strong>{{ item.title }}</strong>
        <code>{{ item.href }}</code>
        <span>{{ item.description }}</span>
      </a>
    </div>
  </section>

  <section class="cards two-columns section-block">
    <article class="card">
      <p class="eyebrow">Prepared capabilities</p>
      <h2>What this template already solves</h2>
      <ul class="check-list spacious-list">
        <li v-for="item in capabilities" :key="item">{{ item }}</li>
      </ul>
    </article>

    <article class="card">
      <p class="eyebrow">Project map</p>
      <h2>Where to work</h2>
      <div class="project-map">
        <div v-for="area in projectAreas" :key="area.name" class="project-row">
          <code>{{ area.name }}</code>
          <p>{{ area.description }}</p>
        </div>
      </div>
      <RouterLink class="button ghost" to="/">Back to welcome page</RouterLink>
    </article>
  </section>

  <section class="card final-callout">
    <div>
      <p class="eyebrow">Frontend ownership</p>
      <h2>This is intentionally a clean starting point.</h2>
      <p>
        Not every application needs a runtime XML constructor. Keep the prepared Vue shell, replace the starter
        content, add your routes, consume the already available backend API and extend the C++ backend only
        where your product needs custom power.
      </p>
    </div>
    <a class="button primary" href="/backend/docs">Open documentation</a>
  </section>
</template>
