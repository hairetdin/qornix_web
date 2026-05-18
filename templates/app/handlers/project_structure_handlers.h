#pragma once

#include "app_paths.h"
#include "handler_base.h"
#include "project_structure_service.h"

#include <boost/json/serialize.hpp>

#include <filesystem>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>

namespace project_structure_handlers {

inline std::string queryParam(const urls::url_view& url, const std::string& key) {
    for (const auto& param : url.params()) {
        if (std::string(param.key) == key) {
            return std::string(param.value);
        }
    }
    return "";
}

inline qornix::project_structure::ProjectStructureService makeService() {
    return qornix::project_structure::ProjectStructureService(qornix_app_paths::sourceDir(), "@PROJECT_NAME@");
}

inline std::string projectStructureHtml() {
    return R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Project Structure · @PROJECT_NAME@</title>
  <link rel="stylesheet" href="/static/app.css">
  <style>
    body.qx-project-page{
      margin:0;
      min-height:100vh;
      color:#1e293b;
      font-family:Inter,system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;
      background:
        radial-gradient(circle at top left, rgba(59,130,246,.22), transparent 30%),
        radial-gradient(circle at top right, rgba(124,92,255,.18), transparent 26%),
        linear-gradient(180deg,#081120 0%,#0d1830 52%,#0b1426 100%);
    }
    .qx-project-page .qx-shell{width:min(1180px,calc(100vw - 32px));margin:0 auto;padding:24px 0 64px}
    .qx-project-page .qx-topbar{display:flex;align-items:center;justify-content:space-between;gap:20px;margin-bottom:32px;color:#fff}
    .qx-project-page .qx-brand{display:inline-flex;align-items:center;gap:12px;color:#fff;text-decoration:none}
    .qx-project-page .qx-brand-mark{display:grid;place-items:center;width:42px;height:42px;border-radius:14px;background:#2453d6;color:#fff;font-weight:900;box-shadow:0 12px 28px rgba(37,83,214,.28)}
    .qx-project-page .qx-brand strong{display:block;color:#fff}
    .qx-project-page .qx-brand small{display:block;color:#9fb0cb}
    .qx-project-page .qx-nav{display:flex;flex-wrap:wrap;gap:10px}
    .qx-project-page .qx-nav a{border-radius:999px;padding:8px 12px;background:#eef2ff;color:#2453d6;text-decoration:none;font-weight:700;box-shadow:0 8px 20px rgba(15,23,42,.12)}
    .qx-project-page .qx-nav a:hover{background:#dfe7ff;color:#1d4ed8}
    .qx-project-page .qx-hero{border:1px solid rgba(148,163,184,.28);border-radius:28px;background:#fff;color:#1e293b;padding:36px;margin-bottom:24px;box-shadow:0 20px 60px rgba(15,23,42,.08)}
    .qx-project-page .qx-hero h1{margin:0 0 14px;color:#0f172a;font-size:clamp(28px,4vw,44px);line-height:1.12;letter-spacing:-.03em}
    .qx-project-page .qx-kicker{margin:0 0 12px;text-transform:uppercase;letter-spacing:.12em;font-size:12px;color:#2453d6;font-weight:800}
    .qx-project-page .qx-lead{color:#475569;font-size:18px;max-width:820px;line-height:1.7}
    .qx-project-page .qx-card{border:1px solid rgba(148,163,184,.28);border-radius:24px;background:#fff;color:#1e293b;padding:28px;box-shadow:0 16px 44px rgba(15,23,42,.06)}
    .qx-project-page .qx-card h1,.qx-project-page .qx-card h2,.qx-project-page .qx-card h3{margin:0 0 10px;color:#0f172a}
    .qx-project-page .qx-card p{color:#475569;line-height:1.6}
    .qx-project-page .qx-actions{display:flex;gap:12px;flex-wrap:wrap;margin-top:18px}
    .qx-project-page .qx-btn{display:inline-flex;align-items:center;justify-content:center;border-radius:999px;background:#2453d6;color:#fff;text-decoration:none;font-weight:800;padding:10px 15px;border:0;cursor:pointer;box-shadow:0 10px 24px rgba(37,83,214,.24)}
    .qx-project-page .qx-btn:hover{background:#1d4ed8;color:#fff}
    .qx-project-page .qx-btn-secondary{background:#eef2ff;color:#2453d6;box-shadow:none}
    .qx-project-page .qx-btn-secondary:hover{background:#dfe7ff;color:#1d4ed8}
    .qx-project-page .qx-browser-head{display:flex;justify-content:space-between;gap:16px;align-items:flex-start;margin-bottom:18px}
    .qx-project-page .qx-breadcrumbs{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin:0 0 16px}
    .qx-project-page .qx-breadcrumbs button{border:0;background:#e8eefc;color:#2453d6;border-radius:999px;padding:8px 12px;font-weight:700;cursor:pointer;box-shadow:none}
    .qx-project-page .qx-breadcrumbs button:hover{background:#dbe6ff;color:#1d4ed8}
    .qx-project-page .qx-table{width:100%;border-collapse:collapse;color:#263449;background:#fff}
    .qx-project-page .qx-table th,.qx-project-page .qx-table td{text-align:left;border-bottom:1px solid rgba(148,163,184,.28);padding:12px;vertical-align:top;color:#334155}
    .qx-project-page .qx-table th{font-size:12px;text-transform:uppercase;letter-spacing:.08em;color:#64748b;background:#fff;font-weight:800}
    .qx-project-page .qx-file-btn{border:0;background:transparent;color:#2453d6;font-weight:800;cursor:pointer;padding:0;box-shadow:none}
    .qx-project-page .qx-file-btn:hover{color:#1d4ed8;text-decoration:underline}
    .qx-project-page .qx-muted{color:#64748b}
    .qx-project-page .qx-kind{display:inline-block;border-radius:999px;background:#eef2ff;color:#2453d6;padding:4px 8px;font-size:12px;font-weight:800}
    .qx-project-page .qx-error{color:#b91c1c;font-weight:700}
    .qx-project-page .qx-toolbar{display:flex;gap:12px;flex-wrap:wrap}
    .qx-project-page .qx-small{font-size:13px}
    @media(max-width:760px){
      .qx-project-page .qx-shell{width:min(100% - 20px,1180px)}
      .qx-project-page .qx-topbar,.qx-project-page .qx-browser-head{flex-direction:column;align-items:flex-start}
      .qx-project-page .qx-nav{width:100%}
      .qx-project-page .qx-nav a{flex:1 1 calc(50% - 10px);text-align:center}
      .qx-project-page .qx-card{padding:22px}
    }
  </style>
</head>
<body class="qx-project-page">
<div class="qx-shell">
  <header class="qx-topbar">
    <a class="qx-brand" href="/"><span class="qx-brand-mark">Qx</span><span><strong>@PROJECT_NAME@</strong><small>Project browser</small></span></a>
    <nav class="qx-nav"><a href="/">Home</a><a href="/project-structure">Project Structure</a><a href="/docs">Docs</a><a href="/health">Health</a></nav>
  </header>

  <section class="qx-hero">
    <div>
      <p class="qx-kicker">Generated project structure</p>
      <h1>Browse the generated application and download a clean project snapshot.</h1>
      <p class="qx-lead">Use this page to understand where backend handlers, routes, templates, static assets and frontend sources live. The archive endpoint excludes build artifacts, dependency folders and Git metadata.</p>
      <div class="qx-actions"><a class="qx-btn" href="/api/project/archive">Download project archive</a><a class="qx-btn qx-btn-secondary" href="/docs">Open docs</a></div>
    </div>
  </section>

  <section class="qx-card">
    <div class="qx-browser-head">
      <div><h2>File browser</h2><p class="qx-muted">Click a directory name to open it. Breadcrumbs show your current location.</p></div>
      <div class="qx-toolbar"><button class="qx-btn qx-btn-secondary" id="refreshBtn" type="button">Refresh</button></div>
    </div>
    <nav class="qx-breadcrumbs" id="breadcrumbs" aria-label="Project path"></nav>
    <div id="error" class="qx-error"></div>
    <table class="qx-table">
      <thead><tr><th>Name</th><th>Kind</th><th>Description</th><th>Size</th></tr></thead>
      <tbody id="entries"><tr><td colspan="4" class="qx-muted">Loading project structure...</td></tr></tbody>
    </table>
  </section>
</div>
<script>
let currentPath = '';
const entriesEl = document.getElementById('entries');
const crumbsEl = document.getElementById('breadcrumbs');
const errorEl = document.getElementById('error');
function formatSize(bytes){if(!bytes)return '—';const units=['B','KB','MB','GB'];let value=bytes,unit=0;while(value>=1024&&unit<units.length-1){value/=1024;unit++;}return `${value.toFixed(value>=10||unit===0?0:1)} ${units[unit]}`;}
function openPath(path){currentPath=path||'';load();}
function renderCrumbs(crumbs){crumbsEl.innerHTML='';(crumbs||[{label:'@PROJECT_NAME@',path:''}]).forEach((crumb,index)=>{const btn=document.createElement('button');btn.type='button';btn.textContent=(index?'› ':'')+crumb.label;btn.onclick=()=>openPath(crumb.path);crumbsEl.appendChild(btn);});}
function renderRows(entries){if(!entries.length){entriesEl.innerHTML='<tr><td colspan="4" class="qx-muted">This directory is empty.</td></tr>';return;}entriesEl.innerHTML='';entries.forEach(entry=>{const tr=document.createElement('tr');const name=document.createElement('td');if(entry.navigable){const btn=document.createElement('button');btn.type='button';btn.className='qx-file-btn';btn.textContent=entry.name;btn.onclick=()=>openPath(entry.path);name.appendChild(btn);}else{name.textContent=entry.name;}const kind=document.createElement('td');kind.innerHTML=`<span class="qx-kind">${entry.type==='directory'?'directory':entry.kind}</span>`;const desc=document.createElement('td');desc.textContent=entry.description||'';const size=document.createElement('td');size.className='qx-small qx-muted';size.textContent=formatSize(entry.size);tr.append(name,kind,desc,size);entriesEl.appendChild(tr);});}
async function load(){errorEl.textContent='';entriesEl.innerHTML='<tr><td colspan="4" class="qx-muted">Loading project structure...</td></tr>';try{const res=await fetch('/api/project/structure?path='+encodeURIComponent(currentPath));const payload=await res.json();renderCrumbs(payload.breadcrumbs);if(!payload.ok){errorEl.textContent=payload.error||'Project directory could not be loaded';renderRows([]);return;}renderRows(payload.entries||[]);}catch(error){errorEl.textContent=error.message||'Project directory could not be loaded';renderRows([]);}}
document.getElementById('refreshBtn').onclick=load;load();
</script>
</body>
</html>)HTML";
}

} // namespace project_structure_handlers

class ProjectStructurePageHandler : public HandlerBase {
protected:
    void handleGet(const http::request<http::string_body>&, http::response<http::string_body>& res, const urls::url_view&, const std::map<std::string, std::string>&) override {
        buildHtmlResponse(res, http::status::ok, project_structure_handlers::projectStructureHtml());
    }
};

class ProjectStructureListHandler : public HandlerBase {
protected:
    void handleGet(const http::request<http::string_body>&, http::response<http::string_body>& res, const urls::url_view& url, const std::map<std::string, std::string>&) override {
        const auto path = project_structure_handlers::queryParam(url, "path");
        auto service = project_structure_handlers::makeService();
        auto payload = service.listDirectory(path);
        buildJsonResponse(res, http::status::ok, boost::json::serialize(payload));
    }
};

class ProjectArchiveHandler : public HandlerBase {
protected:
    void handleGet(const http::request<http::string_body>& req, http::response<http::string_body>& res, const urls::url_view&, const std::map<std::string, std::string>&) override {
        auto service = project_structure_handlers::makeService();
        std::string error;
        const auto archivePath = service.createProjectArchive(error);
        if (archivePath.empty()) {
            boost::json::object payload;
            payload["error"] = error.empty() ? "failed to create project archive" : error;
            res = make_json_response(http::status::internal_server_error, req.version(), boost::json::serialize(payload));
            return;
        }
        auto body = qornix::project_structure::readFileBinary(archivePath);
        std::error_code ec;
        std::filesystem::remove(archivePath, ec);
        res = http::response<http::string_body>{http::status::ok, req.version()};
        res.set(http::field::content_type, "application/gzip");
        res.set(http::field::content_disposition, "attachment; filename=\"@PROJECT_NAME@-project.tar.gz\"");
        res.body() = std::move(body);
        res.prepare_payload();
    }
};

inline std::shared_ptr<ProjectStructurePageHandler> makeProjectStructurePageHandler() { return std::make_shared<ProjectStructurePageHandler>(); }
inline std::shared_ptr<ProjectStructureListHandler> makeProjectStructureListHandler() { return std::make_shared<ProjectStructureListHandler>(); }
inline std::shared_ptr<ProjectArchiveHandler> makeProjectArchiveHandler() { return std::make_shared<ProjectArchiveHandler>(); }
