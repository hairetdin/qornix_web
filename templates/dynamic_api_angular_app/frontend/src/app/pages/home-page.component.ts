import { Component } from '@angular/core'
import { RouterLink } from '@angular/router'

const readyItems = [
  { label: 'Angular frontend', value: 'served from /' },
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
    title: 'Angular is ready for product work',
    text: 'Standalone components, routing, TypeScript, dev proxy and production build are prepared. Replace these starter pages with your real product screens.',
    href: '/backend-admin',
    action: 'Open developer hub'
  },
  {
    title: 'One build, one runtime bundle',
    text: 'CMake builds the backend, runs the Angular production build and places the compiled SPA into the deploy package.',
    href: '/backend/docs',
    action: 'Read backend docs'
  },
  {
    title: 'Extend backend with fast C++ code',
    text: 'Keep the generated Dynamic API, then add custom handlers and routes for workflows, integrations and business operations that need dedicated logic.',
    href: 'https://github.com/hairetdin/qornix_web',
    action: 'Open Qornix Web'
  }
]

const buildSteps = [
  'Define or adjust your XML schema for the entities your product needs.',
  'Use backend tools to validate schema changes, inspect data and test generated endpoints.',
  'Build Angular screens in frontend/ using the ready backend contract.',
  'Add custom C++ handlers in backend/handlers/ when generic CRUD is not enough.',
  'Run the normal CMake build to package backend and frontend together.'
]

const usefulLinks = [
  {
    label: 'Backend Admin',
    href: '/backend-admin',
    router: true,
    tag: 'Hub',
    description: 'Open the developer control center with backend tools, quick start commands and extension points.'
  },
  {
    label: 'Project Structure',
    href: '/project-structure',
    router: true,
    tag: 'Files',
    description: 'Browse generated backend and frontend files, drill into folders and download a project archive.'
  },
  {
    label: 'Schema Manager',
    href: '/backend/schema-manager',
    tag: 'Schema',
    description: 'Validate XML schema changes, preview SQL migration output and apply updates to the backend model.'
  },
  {
    label: 'Table Browser',
    href: '/backend/table',
    tag: 'Data',
    description: 'Inspect generated entities and records through the prepared backend administration pages.'
  },
  {
    label: 'OpenAPI JSON',
    href: '/api/dynamic/openapi.json',
    tag: 'API',
    description: 'Use the live API contract to generate frontend types, clients or external documentation.'
  },
  {
    label: 'Qornix Web',
    href: 'https://github.com/hairetdin/qornix_web',
    tag: 'C++',
    description: 'Review the framework source, routing model and backend extension patterns behind this app.'
  },
  {
    label: 'Angular Docs',
    href: 'https://angular.dev/overview',
    tag: 'Angular',
    description: 'Learn the Angular platform used by the generated frontend workspace.'
  },
  {
    label: 'Angular CLI',
    href: 'https://angular.dev/tools/cli',
    tag: 'CLI',
    description: 'Use Angular CLI commands for local development, builds and future frontend evolution.'
  }
]

@Component({
  selector: 'app-home-page',
  standalone: true,
  imports: [RouterLink],
  template: `
    <section class="hero gradient-panel">
      <div class="hero-content">
        <p class="eyebrow">Generated fullstack starter</p>
        <h1>Qornix Dynamic Angular App</h1>
        <p class="lead">
          Your C++ backend, Dynamic API, Angular frontend, OpenAPI contract, backend tools and production build
          flow are already connected. Start building product-specific screens instead of assembling infrastructure.
        </p>
        <div class="hero-actions">
          <a routerLink="/backend-admin" class="button primary">Open Backend Admin</a>
          <a routerLink="/project-structure" class="button secondary">View Project Structure</a>
          <a href="/api/dynamic/openapi.json" class="button ghost" target="_blank" rel="noreferrer">Open API Contract</a>
        </div>
      </div>
      <div class="status-card">
        <h2>Ready now</h2>
        <ul>
          @for (item of readyItems; track item.label) {
            <li><span>{{ item.label }}</span><strong>{{ item.value }}</strong></li>
          }
        </ul>
      </div>
    </section>

    <section class="section-block">
      <div class="section-title">
        <div>
          <p class="eyebrow">What you received</p>
          <h2>A backend contract and Angular workspace in one scaffold.</h2>
        </div>
        <p>
          This template keeps the existing Dynamic API backend available while giving frontend developers a modern
          Angular application served from the root page.
        </p>
      </div>
      <div class="cards four-columns">
        @for (card of platformCards; track card.title) {
          <article class="card feature-card">
            <h3>{{ card.title }}</h3>
            <p>{{ card.text }}</p>
            <a [href]="card.href" [target]="card.href.startsWith('http') ? '_blank' : null" rel="noreferrer">{{ card.action }} →</a>
          </article>
        }
      </div>
    </section>

    <section class="section-block split-layout">
      <div>
        <p class="eyebrow">Start building</p>
        <h2>Recommended first steps</h2>
        <ol class="step-list">
          @for (step of buildSteps; track step) {
            <li>{{ step }}</li>
          }
        </ol>
      </div>
      <div class="callout-card">
        <h3>Future-ready, not forced</h3>
        <p>
          This starter does not force an XML-driven UI constructor. It gives you a normal Angular workspace first.
          Later, your team can add schema-driven navigation, entity pages, relation tabs and operation buttons only
          where that abstraction is useful.
        </p>
      </div>
    </section>

    <section class="section-block">
      <div class="section-title compact">
        <div>
          <p class="eyebrow">Useful links</p>
          <h2>Jump into the prepared environment.</h2>
        </div>
      </div>
      <div class="link-grid">
        @for (link of usefulLinks; track link.label) {
          @if (link.router) {
            <a class="link-tile" [routerLink]="link.href">
              <span class="tile-tag">{{ link.tag }}</span>
              <strong>{{ link.label }}</strong>
              <span>{{ link.description }}</span>
            </a>
          } @else {
            <a class="link-tile" [href]="link.href" [target]="link.href.startsWith('http') ? '_blank' : null" rel="noreferrer">
              <span class="tile-tag">{{ link.tag }}</span>
              <strong>{{ link.label }}</strong>
              <span>{{ link.description }}</span>
            </a>
          }
        }
      </div>
    </section>
  `
})
export class HomePageComponent {
  readonly readyItems = readyItems
  readonly platformCards = platformCards
  readonly buildSteps = buildSteps
  readonly usefulLinks = usefulLinks
}
