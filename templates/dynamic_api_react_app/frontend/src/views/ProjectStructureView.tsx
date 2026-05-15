import { useEffect, useMemo, useState } from 'react'

type Breadcrumb = {
  label: string
  path: string
}

type ProjectEntry = {
  name: string
  path: string
  kind: string
  type: 'directory' | 'file'
  description: string
  size: number
  downloadable: boolean
  navigable: boolean
}

type ProjectStructureResponse = {
  ok: boolean
  rootLabel: string
  path: string
  breadcrumbs: Breadcrumb[]
  entries: ProjectEntry[]
  error?: string
}

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

function iconClass(entry: ProjectEntry) {
  return entry.type === 'directory' ? 'folder' : entry.kind.replace(/[^a-z0-9_-]/gi, '-')
}

export default function ProjectStructureView() {
  const [currentPath, setCurrentPath] = useState('')
  const [data, setData] = useState<ProjectStructureResponse | null>(null)
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState('')

  useEffect(() => {
    const controller = new AbortController()
    setLoading(true)
    setError('')

    fetch(`/api/project/structure?path=${encodeURIComponent(currentPath)}`, { signal: controller.signal })
      .then(async (response) => {
        if (!response.ok) {
          throw new Error(`Project structure request failed with ${response.status}`)
        }
        return response.json() as Promise<ProjectStructureResponse>
      })
      .then((payload) => {
        setData(payload)
        if (!payload.ok) {
          setError(payload.error || 'Project directory could not be loaded')
        }
      })
      .catch((requestError) => {
        if (requestError.name !== 'AbortError') {
          setError(requestError.message || 'Project directory could not be loaded')
        }
      })
      .finally(() => setLoading(false))

    return () => controller.abort()
  }, [currentPath])

  const parentPath = useMemo(() => {
    if (!currentPath) return ''
    const parts = currentPath.split('/').filter(Boolean)
    parts.pop()
    return parts.join('/')
  }, [currentPath])

  const entries = data?.entries ?? []
  const breadcrumbs = data?.breadcrumbs ?? [{ label: '@PROJECT_NAME@', path: '' }]

  return (
    <>
      <section className="admin-hero gradient-panel">
        <p className="eyebrow">Project structure</p>
        <h1>Browse the generated project and download a clean snapshot.</h1>
        <p className="lead">
          The React template is not a hidden black box. Explore the Qornix Web C++ backend, the React/Vite frontend
          workspace and the CMake deploy output as a guided file table with breadcrumbs.
        </p>
        <div className="actions">
          <a className="button primary" href="/api/project/archive" download>
            Download project archive
          </a>
          <a className="button" href="/backend-admin">Open backend admin</a>
          <a className="button ghost" href="/api/dynamic/openapi.json" target="_blank" rel="noreferrer">OpenAPI</a>
        </div>
      </section>

      <section className="section-block split-panel">
        <article className="card emphasis-card">
          <p className="eyebrow">Recommended workflow</p>
          <h2>Backend contract first, frontend product work next.</h2>
          <p>
            Keep generated backend capabilities in <code>backend/</code>, develop product screens in <code>frontend/</code>,
            and use <code>build/deploy/@PROJECT_NAME@/</code> as the packaged runtime output. Custom high-performance C++
            logic can be added to handlers and routes whenever the product needs more than generated CRUD/query endpoints.
          </p>
        </article>

        <article className="card code-card">
          <p className="eyebrow">Archive endpoint</p>
          <h2>Share the current scaffold</h2>
          <p>
            The generated backend exposes a local archive endpoint for convenient handoff. It creates a compressed
            snapshot of the current project root while excluding heavy development artifacts.
          </p>
          <pre><code>{`GET /api/project/archive
# downloads @PROJECT_NAME@-project.tar.gz`}</code></pre>
        </article>
      </section>

      <section className="section-block">
        <div className="section-title">
          <div>
            <p className="eyebrow">Generated layout</p>
            <h2>File browser</h2>
          </div>
          <p>
            Click directories to move deeper into the generated project. Breadcrumbs above the table show your current
            location and let you jump back to any parent folder.
          </p>
        </div>

        <div className="card file-browser-card">
          <nav className="breadcrumbs" aria-label="Project path">
            {breadcrumbs.map((crumb, index) => (
              <button
                key={`${crumb.path}-${index}`}
                type="button"
                className="breadcrumb-link"
                onClick={() => setCurrentPath(crumb.path)}
              >
                {crumb.label}
              </button>
            ))}
          </nav>

          <div className="file-browser-toolbar">
            <div>
              <strong>{currentPath || '@PROJECT_NAME@/'}</strong>
              <span>{loading ? 'Loading directory...' : `${entries.length} item${entries.length === 1 ? '' : 's'}`}</span>
            </div>
            {currentPath && (
              <button className="button small ghost" type="button" onClick={() => setCurrentPath(parentPath)}>
                Up one level
              </button>
            )}
          </div>

          {error && <div className="inline-alert">{error}</div>}

          <div className="table-wrap">
            <table className="project-table">
              <thead>
                <tr>
                  <th>Name</th>
                  <th>Type</th>
                  <th>Size</th>
                  <th>Description</th>
                </tr>
              </thead>
              <tbody>
                {entries.map((entry) => (
                  <tr key={entry.path}>
                    <td>
                      {entry.navigable ? (
                        <button className="file-name-button" type="button" onClick={() => setCurrentPath(entry.path)}>
                          <span className="file-icon folder" aria-hidden="true" />
                          {entry.name}
                        </button>
                      ) : (
                        <span className="file-name-static">
                          <span className={`file-icon ${iconClass(entry)}`} aria-hidden="true" />
                          {entry.name}
                        </span>
                      )}
                    </td>
                    <td><span className="kind-pill">{kindLabel(entry)}</span></td>
                    <td>{formatSize(entry.size)}</td>
                    <td>{entry.description}</td>
                  </tr>
                ))}
                {!loading && entries.length === 0 && (
                  <tr>
                    <td colSpan={4} className="empty-table">No visible files in this directory.</td>
                  </tr>
                )}
              </tbody>
            </table>
          </div>
        </div>
      </section>

      <section className="cards two-columns section-block">
        <article className="card">
          <p className="eyebrow">Download notes</p>
          <h2>What the archive contains</h2>
          <ul className="check-list spacious-list">
            {downloadNotes.map((note) => <li key={note}>{note}</li>)}
          </ul>
        </article>
        <article className="card">
          <p className="eyebrow">Where to continue</p>
          <h2>Next implementation points</h2>
          <div className="project-map compact-map">
            <div className="project-row">
              <strong>React pages</strong>
              <code>frontend/src/views/</code>
              <p>Add product screens, dashboards and workflows here.</p>
            </div>
            <div className="project-row">
              <strong>Backend operations</strong>
              <code>backend/handlers/ + backend/routes.h</code>
              <p>Add custom C++ endpoints for business actions, reports or integrations.</p>
            </div>
            <div className="project-row">
              <strong>API contract</strong>
              <code>/api/dynamic/openapi.json</code>
              <p>Use the generated contract for frontend types, clients and manual inspection.</p>
            </div>
          </div>
        </article>
      </section>
    </>
  )
}
