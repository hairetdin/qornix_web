import { Component } from '@angular/core'
import { RouterLink } from '@angular/router'

const backendTools = [
  { title: 'Backend Home', href: '/backend', tag: 'HTML', description: 'Legacy backend landing page with links into the generated Dynamic API tools.' },
  { title: 'Schema Manager', href: '/backend/schema-manager', tag: 'Schema', description: 'Validate XML schema, preview SQL changes and apply schema updates.' },
  { title: 'Query Builder', href: '/backend/query-builder', tag: 'Queries', description: 'Build dynamic API queries and inspect backend responses.' },
  { title: 'Table Browser', href: '/backend/table', tag: 'Data', description: 'Browse and edit generated entity records through backend pages.' },
  { title: 'API Playground', href: '/backend/api-playground', tag: 'API', description: 'Test backend endpoints directly from the browser.' },
  { title: 'Backend Docs', href: '/backend/docs', tag: 'Docs', description: 'Review generated backend documentation and usage examples.' },
  { title: 'Query Syntax', href: '/backend/docs/query-syntax', tag: 'Docs', description: 'Learn filter, join and dynamic query syntax.' },
  { title: 'Schema Docs', href: '/backend/docs/schema', tag: 'Docs', description: 'Review XML schema conventions and Schema Manager workflow details.' },
  { title: 'Metadata: tables', href: '/api/dynamic/meta/tables', tag: 'JSON', description: 'Discover generated tables from frontend code or manual inspection.' },
  { title: 'OpenAPI JSON', href: '/api/dynamic/openapi.json', tag: 'Contract', description: 'Generate TypeScript types, API clients or documentation from the live backend contract.' },
  { title: 'Health Check', href: '/health', tag: 'Runtime', description: 'Minimal endpoint for smoke tests, reverse proxies, containers and deployment checks.' },
  { title: 'Project Archive', href: '/api/project/archive', tag: 'Download', description: 'Download a compressed snapshot of the generated project.' }
]

const capabilities = [
  'Generated Dynamic CRUD/query API under /api/dynamic/*',
  'Metadata endpoints for frontend discovery and debugging',
  'Custom C++ backend handlers and routes for product-specific logic',
  'Schema Manager for XML validation, SQL preview and apply workflow',
  'Query Builder and Table Browser for manual API exploration',
  'OpenAPI document for generated frontend types and SDKs',
  'Qornix Web framework foundation with reusable routing and handler patterns',
  'Angular + TypeScript scaffold served as the application homepage',
  'Angular dev-server proxy to the running backend',
  'CMake-integrated frontend production build and deploy bundle copy',
  'Legacy backend pages preserved under /backend/* instead of occupying /',
  'Project structure page and archive download for easier handoff'
]

const projectAreas = [
  { name: 'backend/', description: 'C++ application, routes, config, schema files, backend templates and static debug assets. Add custom handlers here when Dynamic API is not enough.' },
  { name: 'frontend/', description: 'Angular application source. This is the main workspace for product UI development.' },
  { name: 'build/deploy/@PROJECT_NAME@/', description: 'Portable runtime bundle with compiled backend, backend assets and production frontend build.' }
]

const cppExtensionPoints = [
  { title: 'Handlers', path: 'backend/handlers/', description: 'Put reusable C++ request handlers, business operations and integrations here.' },
  { title: 'Routes', path: 'backend/routes.h', description: 'Register new HTTP endpoints next to the prepared Dynamic API and backend pages.' },
  { title: 'Operations API', path: '/api/operations/*', description: 'Recommended namespace for product-specific actions that are not generic CRUD.' }
]

const docsLinks = [
  { title: 'Qornix Web', href: 'https://github.com/hairetdin/qornix_web', description: 'Framework source, examples and backend extension patterns.' },
  { title: 'Angular', href: 'https://angular.dev/overview', description: 'Components, templates, routing and Angular application architecture.' },
  { title: 'Angular CLI', href: 'https://angular.dev/tools/cli', description: 'Development server, build system and Angular project tooling.' },
  { title: 'RxJS', href: 'https://rxjs.dev/guide/overview', description: 'Reactive primitives used by Angular HTTP and application flows.' }
]

@Component({
  selector: 'app-backend-admin-page',
  standalone: true,
  imports: [RouterLink],
  template: `
    <section class="admin-hero gradient-panel">
      <p class="eyebrow">Developer control center</p>
      <h1>Backend tools, API contract and Angular workspace in one place.</h1>
      <p class="lead">
        This page is the generated project hub. Use it to inspect the ready Dynamic API, validate schemas,
        test requests, understand the deploy layout and decide where to add custom C++ backend logic.
      </p>
    </section>

    <section class="section-block">
      <div class="section-title">
        <div>
          <p class="eyebrow">Workflow</p>
          <h2>From schema to product UI</h2>
        </div>
        <p>
          Start with the generated backend contract, build Angular screens on top of it, and add dedicated C++
          operations only where the product requires behavior beyond generic CRUD/query endpoints.
        </p>
      </div>
      <div class="flow-row">
        <span>Schema</span><i aria-hidden="true">→</i><span>Dynamic API</span><i aria-hidden="true">→</i>
        <span>Angular frontend</span><i aria-hidden="true">→</i><span>C++ operations</span><i aria-hidden="true">→</i><span>Deploy bundle</span>
      </div>
    </section>

    <section class="cards two-columns quickstart-grid">
      <article class="card">
        <p class="eyebrow">Backend quick start</p>
        <h2>Run the generated C++ application</h2>
        <pre><code>mkdir -p build
cd build
cmake ..
cmake --build .
./@PROJECT_NAME@</code></pre>
      </article>
      <article class="card">
        <p class="eyebrow">Frontend quick start</p>
        <h2>Work in Angular dev mode</h2>
        <pre><code>cd frontend
npm install
npm run dev</code></pre>
        <p class="muted">
          The Angular dev server proxies /api, /backend and /health to the running C++ backend.
        </p>
      </article>
    </section>

    <section class="section-block">
      <div class="section-title">
        <div>
          <p class="eyebrow">Backend extension points</p>
          <h2>Add your own fast C++ behavior when the generated API is not enough.</h2>
        </div>
        <p>
          Dynamic API is excellent for generic data access. Product workflows, integrations, reports and heavy
          operations can be added as explicit C++ handlers without replacing the generated backend.
        </p>
      </div>
      <div class="cards three-columns">
        @for (point of cppExtensionPoints; track point.title) {
          <article class="card compact-card">
            <h3>{{ point.title }}</h3>
            <code>{{ point.path }}</code>
            <p>{{ point.description }}</p>
          </article>
        }
      </div>
    </section>

    <section class="section-block">
      <div class="section-title">
        <div>
          <p class="eyebrow">Backend tools</p>
          <h2>Prepared pages and endpoints</h2>
        </div>
        <p>Everything below is available from the generated backend while your Angular app evolves.</p>
      </div>
      <div class="tool-grid">
        @for (tool of backendTools; track tool.title) {
          <a class="tool-card" [href]="tool.href" target="_blank" rel="noreferrer">
            <span>{{ tool.tag }}</span>
            <strong>{{ tool.title }}</strong>
            <small>{{ tool.description }}</small>
          </a>
        }
      </div>
    </section>

    <section class="section-block split-layout">
      <div>
        <p class="eyebrow">Capabilities</p>
        <h2>What this generated app can already do</h2>
        <ul class="check-list">
          @for (item of capabilities; track item) {
            <li>{{ item }}</li>
          }
        </ul>
      </div>
      <div class="callout-card">
        <h3>Inspect and share the project</h3>
        <p>
          Use the Project Structure page to browse the generated scaffold as a table with breadcrumbs,
          then download the archive when you want to hand it to another developer.
        </p>
        <a routerLink="/project-structure" class="button primary">Open Project Structure</a>
      </div>
    </section>

    <section class="section-block">
      <div class="section-title">
        <div>
          <p class="eyebrow">Project map</p>
          <h2>Where to work</h2>
        </div>
      </div>
      <div class="cards three-columns">
        @for (area of projectAreas; track area.name) {
          <article class="card compact-card">
            <h3><code>{{ area.name }}</code></h3>
            <p>{{ area.description }}</p>
          </article>
        }
      </div>
    </section>

    <section class="section-block">
      <div class="section-title">
        <div>
          <p class="eyebrow">Documentation</p>
          <h2>Framework and frontend references</h2>
        </div>
      </div>
      <div class="cards four-columns">
        @for (doc of docsLinks; track doc.title) {
          <a class="card doc-card" [href]="doc.href" target="_blank" rel="noreferrer">
            <h3>{{ doc.title }}</h3>
            <p>{{ doc.description }}</p>
          </a>
        }
      </div>
    </section>
  `
})
export class BackendAdminPageComponent {
  readonly backendTools = backendTools
  readonly capabilities = capabilities
  readonly projectAreas = projectAreas
  readonly cppExtensionPoints = cppExtensionPoints
  readonly docsLinks = docsLinks
}
