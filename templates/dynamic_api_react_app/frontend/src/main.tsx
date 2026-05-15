import React from 'react'
import { createRoot } from 'react-dom/client'
import { BrowserRouter, Link, NavLink, Route, Routes } from 'react-router-dom'
import HomeView from './views/HomeView'
import BackendAdminView from './views/BackendAdminView'
import './style.css'

function App() {
  return (
    <div className="app-shell">
      <header className="topbar">
        <Link className="brand" to="/">
          <span className="brand-mark">Qx</span>
          <span>
            <strong>Qornix Dynamic React</strong>
            <small>Backend API + React scaffold</small>
          </span>
        </Link>
        <nav className="nav-links" aria-label="Main navigation">
          <NavLink to="/">Home</NavLink>
          <NavLink to="/backend-admin">Backend Admin</NavLink>
          <a href="/api/dynamic/openapi.json" target="_blank" rel="noreferrer">OpenAPI</a>
        </nav>
      </header>

      <main>
        <Routes>
          <Route path="/" element={<HomeView />} />
          <Route path="/backend-admin" element={<BackendAdminView />} />
        </Routes>
      </main>
    </div>
  )
}

createRoot(document.getElementById('root') as HTMLElement).render(
  <React.StrictMode>
    <BrowserRouter>
      <App />
    </BrowserRouter>
  </React.StrictMode>
)
