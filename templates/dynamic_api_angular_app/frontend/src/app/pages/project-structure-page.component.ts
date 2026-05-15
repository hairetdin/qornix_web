import { AsyncPipe } from '@angular/common'
import { Component } from '@angular/core'
import { BehaviorSubject, catchError, map, of, switchMap, tap } from 'rxjs'
import { ProjectEntry, ProjectStructureApiService } from '../services/project-structure-api.service'

const downloadNotes = [
  'The archive endpoint is intended for local development and onboarding.',
  'The archive excludes build artifacts, node_modules, frontend/dist and Git metadata.',
  'Use the archive to hand the generated scaffold to another developer or keep a snapshot of the current starter project.'
]

function formatSize(bytes: number) {
  if (!bytes) return '—'
  const units = ['B', 'KB', 'MB', 'GB']
  let value = bytes
  let unit = 0
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024
    unit += 1
  }
  return `${value.toFixed(value >= 10 || unit === 0 ? 0 : 1)} ${units[unit]}`
}

function kindLabel(entry: ProjectEntry) {
  if (entry.type === 'directory') return 'Directory'
  return entry.kind
}

@Component({
  selector: 'app-project-structure-page',
  standalone: true,
  imports: [AsyncPipe],
  template: `
    <section class="admin-hero gradient-panel">
      <p class="eyebrow">Generated project handoff</p>
      <h1>Project Structure</h1>
      <p class="lead">
        Browse the generated scaffold as a table, drill into folders with breadcrumbs, and download a clean project
        archive for handoff or review.
      </p>
      <div class="hero-actions">
        <a class="button primary" href="/api/project/archive">Download Project Archive</a>
        <a class="button secondary" href="/backend-admin">Open Backend Admin</a>
      </div>
    </section>

    <section class="section-block split-layout">
      <div>
        <p class="eyebrow">How to use this page</p>
        <h2>Understand the generated layout before editing.</h2>
        <p>
          The backend endpoint returns only safe project paths. Build outputs, node_modules, Git metadata and runtime
          noise are excluded from the downloadable archive.
        </p>
      </div>
      <div class="callout-card">
        <h3>Download notes</h3>
        <ul class="check-list compact">
          @for (note of downloadNotes; track note) {
            <li>{{ note }}</li>
          }
        </ul>
      </div>
    </section>

    @if (viewModel$ | async; as vm) {
      <section class="section-block">
        <div class="structure-toolbar">
          <div>
            <p class="eyebrow">Current path</p>
            <div class="breadcrumbs" aria-label="Project path breadcrumbs">
              @for (crumb of vm.data?.breadcrumbs || []; track crumb.path) {
                <button type="button" (click)="openPath(crumb.path)">{{ crumb.label }}</button>
                @if (!$last) {
                  <span>/</span>
                }
              }
            </div>
          </div>
          <button type="button" class="button secondary" (click)="refresh()">Refresh</button>
        </div>

        @if (vm.loading) {
          <div class="loading-box">Loading project structure...</div>
        }
        @if (vm.error) {
          <div class="error-box">{{ vm.error }}</div>
        }

        <div class="table-card">
          <table class="structure-table">
            <thead>
              <tr>
                <th>Name</th>
                <th>Type</th>
                <th>Size</th>
                <th>Relative path</th>
                <th>Description</th>
              </tr>
            </thead>
            <tbody>
              @for (entry of vm.data?.entries || []; track entry.path) {
                <tr [class.folder-row]="entry.type === 'directory'">
                  <td>
                    @if (entry.navigable) {
                      <button type="button" class="file-button" (click)="openPath(entry.path)">
                        <span class="file-icon folder">{{ entry.type === 'directory' ? '📁' : '📄' }}</span>
                        {{ entry.name }}
                      </button>
                    } @else {
                      <span class="file-name">
                        <span class="file-icon">📄</span>
                        {{ entry.name }}
                      </span>
                    }
                  </td>
                  <td><span class="pill">{{ kindLabel(entry) }}</span></td>
                  <td>{{ formatSize(entry.size) }}</td>
                  <td><code>{{ entry.path || '.' }}</code></td>
                  <td>{{ entry.description }}</td>
                </tr>
              } @empty {
                <tr>
                  <td colspan="5" class="empty-cell">No project entries found for this path.</td>
                </tr>
              }
            </tbody>
          </table>
        </div>
      </section>
    }
  `
})
export class ProjectStructurePageComponent {
  private readonly path$ = new BehaviorSubject('')

  readonly downloadNotes = downloadNotes
  readonly formatSize = formatSize
  readonly kindLabel = kindLabel

  readonly viewModel$ = this.path$.pipe(
    switchMap((path) => {
      const loading = of({ loading: true, error: '', data: null })
      const request = this.api.list(path).pipe(
        map((data) => ({ loading: false, error: data.ok ? '' : data.error || 'Project directory could not be loaded', data })),
        catchError((error: unknown) => of({ loading: false, error: error instanceof Error ? error.message : 'Project structure request failed', data: null }))
      )
      return loading.pipe(switchMap(() => request), tap(() => undefined))
    })
  )

  constructor(private readonly api: ProjectStructureApiService) {}

  openPath(path: string) {
    this.path$.next(path || '')
  }

  refresh() {
    this.path$.next(this.path$.value)
  }
}
