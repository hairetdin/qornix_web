const backendLinks = [
  { title: 'Backend Home', href: '/backend', tag: 'Overview', description: 'Original Dynamic API landing page with entry points to backend tools and demo workflow.' },
  { title: 'Schema Manager', href: '/backend/schema-manager', tag: 'Schema', description: 'Validate XML, preview generated SQL, build a migration plan and apply schema changes safely.' },
  { title: 'Query Builder', href: '/backend/query-builder', tag: 'Queries', description: 'Compose Dynamic API query parameters visually before wiring them into frontend screens.' },
  { title: 'Table Browser', href: '/backend/table', tag: 'Data', description: 'Inspect available tables, rows, filters and record details from the generated backend UI.' },
  { title: 'API Playground', href: '/backend/api-playground', tag: 'Testing', description: 'Try generated backend requests without leaving the application or opening external tools.' },
  { title: 'Documentation', href: '/backend/docs', tag: 'Docs', description: 'Open the documentation hub for Dynamic API usage, schema guides and backend references.' },
  { title: 'Query Syntax', href: '/backend/docs/query-syntax', tag: 'Docs', description: 'Learn the filter, sorting, pagination and relation syntax exposed by the Dynamic API.' },
  { title: 'Schema Docs', href: '/backend/docs/schema', tag: 'Docs', description: 'Review XML schema conventions and Schema Manager workflow details.' },
  { title: 'Metadata: tables', href: '/api/dynamic/meta/tables', tag: 'JSON', description: 'Use this endpoint to discover tables from frontend code or during manual inspection.' },
  { title: 'OpenAPI JSON', href: '/api/dynamic/openapi.json', tag: 'Contract', description: 'Generate TypeScript types, API clients or documentation from the live backend contract.' },
  { title: 'Health Check', href: '/health', tag: 'Runtime', description: 'Minimal endpoint for smoke tests, reverse proxies, containers and deployment checks.' }
]

const capabilities = [
  'Generated Dynamic CRUD/query API under /api/dynamic/*',
  'Metadata endpoints for frontend discovery and debugging',
  'Custom C++ backend handlers and routes for product-specific logic',
  'Schema Manager for XML validation, SQL preview and apply workflow',
  'Query Builder and Table Browser for manual API exploration',
  'OpenAPI document for generated frontend types and SDKs',
  'Qornix Web framework foundation with reusable routing and handler patterns',
  'React + Vite + TypeScript scaffold served as the application homepage',
  'Vite development proxy to the running backend',
  'CMake-integrated frontend production build and deploy bundle copy',
  'Legacy backend pages preserved under /backend/* instead of occupying /'
]

const projectAreas = [
  { name: 'backend/', description: 'C++ application, routes, config, schema files, backend templates and static debug assets. Add custom handlers here when Dynamic API is not enough.' },
  { name: 'frontend/', description: 'React application source. This is the main workspace for product UI development.' },
  { name: 'build/deploy/@PROJECT_NAME@/', description: 'Portable runtime bundle with compiled backend, backend assets and production frontend build.' }
]

const cppExtensionPoints = [
  { title: 'Handlers', path: 'backend/handlers/', description: 'Put reusable C++ request handlers, business operations and integrations here.' },
  { title: 'Routes', path: 'backend/routes.h', description: 'Register new HTTP endpoints next to the prepared Dynamic API and backend pages.' },
  { title: 'Operations API', path: '/api/operations/*', description: 'Recommended namespace for product-specific actions that are not generic CRUD.' }
]

const docsLinks = [
  { title: 'Qornix Web', href: 'https://github.com/hairetdin/qornix_web', description: 'Framework source, examples and backend extension patterns.' },
  { title: 'React', href: 'https://react.dev/learn', description: 'Components, hooks, state and frontend architecture.' },
  { title: 'React Router', href: 'https://reactrouter.com/start/declarative/installation', description: 'Client-side routing for product screens and nested flows.' },
  { title: 'Vite', href: 'https://vite.dev/guide/', description: 'Development server, production build and frontend tooling.' }
]

export default function BackendAdminView() {
  return (
    <>
      <section className="admin-hero gradient-panel">
        <p className="eyebrow">Developer control center</p>
        <h1>Backend tools, API contract and React workspace in one place.</h1>
        <p className="lead">
          This page is the generated project hub. Use it to inspect the ready Dynamic API, validate schemas,
          test requests, understand the deploy layout and decide where to add custom C++ backend logic.
        </p>
      </section>

      <section className="section-block">
        <div className="section-title">
          <div>
            <p className="eyebrow">Workflow</p>
            <h2>From schema to product UI</h2>
          </div>
          <p>
            Start with the generated backend contract, build React screens on top of it, and add dedicated C++
            operations only where the product requires behavior beyond generic CRUD/query endpoints.
          </p>
        </div>
        <div className="flow-row">
          <span>Schema</span><i aria-hidden="true">→</i><span>Dynamic API</span><i aria-hidden="true">→</i>
          <span>React frontend</span><i aria-hidden="true">→</i><span>C++ operations</span><i aria-hidden="true">→</i><span>Deploy bundle</span>
        </div>
      </section>

      <section className="cards two-columns quickstart-grid">
        <article className="card">
          <p className="eyebrow">Run the backend bundle</p>
          <h2>Production-like runtime</h2>
          <pre><code>{`mkdir -p build
cd build
cmake ..
cmake --build .
cd deploy/@PROJECT_NAME@
./@PROJECT_NAME@`}</code></pre>
          <p>The CMake build also prepares the frontend production assets that are served by the backend runtime.</p>
        </article>
        <article className="card">
          <p className="eyebrow">Work on the frontend</p>
          <h2>Fast React development</h2>
          <pre><code>{`cd frontend
npm install
npm run dev`}</code></pre>
          <p>The dev server keeps frontend iteration fast while proxying API and backend-tool requests to <code>http://127.0.0.1:8008</code>.</p>
        </article>
      </section>

      <section className="cards two-columns section-block">
        <article className="card">
          <p className="eyebrow">Backend extensibility</p>
          <h2>Add your own fast C++ backend logic</h2>
          <p>
            The template is generated on top of Qornix Web. It is not a black box: keep the Dynamic API for
            standard data access, and add dedicated C++ endpoints when you need custom behavior or maximum control.
          </p>
          <div className="project-map compact-map">
            {cppExtensionPoints.map((point) => (
              <div key={point.path} className="project-row">
                <strong>{point.title}</strong>
                <code>{point.path}</code>
                <p>{point.description}</p>
              </div>
            ))}
          </div>
        </article>
        <article className="card">
          <p className="eyebrow">Recommended pattern</p>
          <h2>Use the right layer for each task</h2>
          <ul className="check-list spacious-list">
            <li>Use <code>/api/dynamic/*</code> for generated CRUD, metadata and query-driven screens.</li>
            <li>Use React routes and components for product UI, dashboards and workflows.</li>
            <li>Use custom C++ handlers for domain operations, integrations and performance-sensitive logic.</li>
            <li>Use OpenAPI and backend docs as the frontend/backend contract.</li>
          </ul>
        </article>
      </section>

      <section className="section-block">
        <div className="section-title">
          <div>
            <p className="eyebrow">Backend links</p>
            <h2>Prepared tools and live endpoints</h2>
          </div>
          <p>These links are served by the generated C++ backend and are useful while building frontend screens.</p>
        </div>
        <div className="cards three-columns">
          {backendLinks.map((link) => (
            <a key={link.href} className="link-tile" href={link.href} target={link.href.startsWith('/api') || link.href === '/health' ? '_blank' : undefined} rel="noreferrer">
              <span className="tile-tag">{link.tag}</span>
              <strong>{link.title}</strong>
              <span>{link.description}</span>
            </a>
          ))}
        </div>
      </section>

      <section className="cards two-columns section-block">
        <article className="card">
          <p className="eyebrow">Capabilities</p>
          <h2>What this template already prepares</h2>
          <ul className="check-list">
            {capabilities.map((item) => <li key={item}>{item}</li>)}
          </ul>
        </article>
        <article className="card">
          <p className="eyebrow">Project map</p>
          <h2>Where to work</h2>
          <div className="project-map">
            {projectAreas.map((area) => (
              <div key={area.name} className="project-row">
                <code>{area.name}</code>
                <p>{area.description}</p>
              </div>
            ))}
          </div>
        </article>
      </section>

      <section className="card final-callout">
        <div>
          <p className="eyebrow">Framework and frontend docs</p>
          <h2>Open the references when you start extending the app</h2>
          <p>Use Qornix Web docs/source for backend extension ideas and React/Vite docs for frontend development.</p>
        </div>
        <div className="resource-list compact-resources">
          {docsLinks.map((link) => (
            <a key={link.href} href={link.href} target="_blank" rel="noreferrer">
              <strong>{link.title}</strong>
              <span>{link.description}</span>
            </a>
          ))}
        </div>
      </section>
    </>
  )
}
