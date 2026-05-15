# Dynamic API Angular application template

This document describes the generated application created with:

```bash
./create_new_project.sh ../my_angular --with-dynamic-api-angular
```

Equivalent explicit form:

```bash
./create_new_project.sh ../my_angular --template dynamic-api-angular
```

The template is located at:

```text
templates/dynamic_api_angular_app
```

## Purpose

`dynamic_api_angular_app` is a fullstack starter for projects that want a ready Qornix Dynamic API backend and an Angular frontend in one generated application.

It is intentionally simple at this stage. It does not implement XML-driven UI generation yet. Instead, it prepares the project boundary that frontend developers need:

- a compiled C++ backend;
- a working Dynamic API under `/api/dynamic/*`;
- OpenAPI JSON under `/api/dynamic/openapi.json`;
- backend/admin/debug pages under `/backend/*`;
- an Angular application served from `/`;
- an Angular developer dashboard under `/backend-admin`;
- an Angular CLI development workflow;
- frontend production build integrated into `cmake --build .`;
- a portable deploy bundle containing both backend and frontend assets.

Use this template when you want to build a real frontend while keeping a ready backend API environment available from day one.

## Generator options

| Command | Result |
|---------|--------|
| `./create_new_project.sh ../my_angular --with-dynamic-api-angular` | Create a Dynamic API + Angular application. |
| `./create_new_project.sh ../my_angular --template dynamic-api-angular` | Same template through explicit `--template`. |
| `./create_new_project.sh ../my_angular --template dynamic_api_angular` | Same template using underscore alias. |
| `./create_new_project.sh ../my_angular --template dynamic_api_angular_app` | Same template using internal template name. |

The older Dynamic API template remains available separately:

```bash
./create_new_project.sh ../my_api --with-dynamic-api
./create_new_project.sh ../my_api --template dynamic-api
```

## Generated layout

```text
my_angular/
├── CMakeLists.txt
├── README.md
├── runtime-Dockerfile
├── backend/
│   ├── main.cpp
│   ├── routes.h
│   ├── config.yaml
│   ├── app_paths.h
│   ├── runtime_config.h
│   ├── handlers/
│   ├── templates/
│   ├── static/
│   ├── schema/
│   └── doc/
├── frontend/
│   ├── package.json
│   ├── angular.json
│   ├── index.html
│   └── src/
├── route_extensions/
└── logs/
```

## Backend routes

The generated backend keeps the Dynamic API routes and moves the legacy backend pages into a separate namespace:

```text
/                         -> Angular SPA
/backend-admin            -> Angular developer dashboard
/backend                  -> backend landing/debug page
/backend/schema-manager   -> Schema Manager
/backend/query-builder    -> Query Builder
/backend/table            -> Table Browser
/backend/api-playground   -> API Playground
/backend/docs             -> backend documentation
/api/dynamic/...          -> Dynamic API
/api/dynamic/openapi.json -> OpenAPI schema
/health                   -> health endpoint
```

Unknown frontend routes are served through the Angular SPA fallback, while `/api/*`, `/backend/*`, `/static/*`, `/assets/*` and `/health` remain backend/static routes.

## Backend development

The backend is a normal Qornix Web application. It is not a closed generated artifact.

You can add C++ handlers, services, routes and business operations when the product needs behavior that is outside the generic Dynamic API:

- custom operations;
- imports and exports;
- integrations with external systems;
- reporting endpoints;
- background task triggers;
- validation workflows;
- performance-sensitive business logic.

The generated project links Qornix Web as a CMake dependency. Framework documentation starts from:

```text
README.md
doc/qornix_create_new_app_instruction.md
doc/schema_driven_dynamic_api.md
doc/dynamic_api_app_deployment.md
doc/roadmap_step_by_step_example.md
```

The public source repository is:

```text
https://github.com/hairetdin/qornix_web
```

## Frontend development

Start the backend first:

```bash
cd ../my_angular
mkdir -p build
cd build
cmake ..
cmake --build .
./my_angular
```

Then start Angular CLI:

```bash
cd ../frontend
npm install
npm run dev
```

The generated `angular.json` proxies backend paths to the running C++ backend, so frontend code can use relative requests:

```ts
fetch('/api/dynamic/meta/tables')
fetch('/api/dynamic/openapi.json')
```

Useful frontend references:

```text
https://angular.dev/overview
https://angular.dev/tools/cli
https://angular.dev/guide/routing
https://angular.dev/guide/http
https://rxjs.dev/guide/overview
```

## Production build

The normal CMake build also builds the frontend:

```bash
cmake --build .
```

CMake runs the frontend build in `frontend/`:

```bash
npm install
npm run build
```

Then it copies `frontend/dist` into the deploy bundle:

```text
build/deploy/my_angular/frontend/
```

`npm` is required for this template. If `npm` is not available, CMake fails with a clear error.

## Deploy bundle

After build, the portable bundle contains backend runtime files and compiled frontend assets:

```text
build/deploy/my_angular/
├── my_angular
├── config.yaml
├── README.md
├── backend/
│   ├── templates/
│   ├── static/
│   ├── schema/
│   └── doc/
├── frontend/
│   ├── index.html
│   └── assets/
└── logs/
```

Run it from the bundle root:

```bash
cd build/deploy/my_angular
./my_angular
```

Open:

```text
http://127.0.0.1:8008/
http://127.0.0.1:8008/backend-admin
http://127.0.0.1:8008/project-structure
http://127.0.0.1:8008/backend/schema-manager
http://127.0.0.1:8008/api/dynamic/openapi.json
```

## Future extension direction

The template can later grow into an XML-driven frontend constructor, but that is intentionally outside the first implementation phase.

A future layer may read UI XML and build:

- navigation;
- entity list pages;
- detail pages;
- editable forms;
- relation tabs;
- operation buttons.

For now, the generated Angular app is a clean frontend workspace that can be customized freely per project.
