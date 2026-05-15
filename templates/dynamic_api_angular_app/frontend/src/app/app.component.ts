import { Component } from '@angular/core'
import { RouterLink, RouterLinkActive, RouterOutlet } from '@angular/router'

@Component({
  selector: 'app-root',
  standalone: true,
  imports: [RouterOutlet, RouterLink, RouterLinkActive],
  template: `
    <div class="app-shell">
      <header class="topbar">
        <a class="brand" routerLink="/">
          <span class="brand-mark">Qx</span>
          <span>
            <strong>Qornix Dynamic Angular</strong>
            <small>Backend API + Angular scaffold</small>
          </span>
        </a>
        <nav class="nav-links" aria-label="Main navigation">
          <a routerLink="/" routerLinkActive="active" [routerLinkActiveOptions]="{ exact: true }">Home</a>
          <a routerLink="/backend-admin" routerLinkActive="active">Backend Admin</a>
          <a routerLink="/project-structure" routerLinkActive="active">Project Structure</a>
          <a href="/api/dynamic/openapi.json" target="_blank" rel="noreferrer">OpenAPI</a>
        </nav>
      </header>

      <main>
        <router-outlet></router-outlet>
      </main>
    </div>
  `
})
export class AppComponent {}
