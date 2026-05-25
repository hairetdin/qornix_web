#!/usr/bin/env bash
# Creates a standalone Qornix Web application from templates/app.

set -euo pipefail

print_usage() {
    cat <<USAGE
Qornix Web application generator

Usage:
  ./create_new_project.sh <project_name_or_path> [--with-dynamic-api]
  ./create_new_project.sh <project_name_or_path> [--with-dynamic-api-vue]
  ./create_new_project.sh <project_name_or_path> [--with-dynamic-api-react]
  ./create_new_project.sh <project_name_or_path> [--with-dynamic-api-angular]
  ./create_new_project.sh <project_name_or_path> [--with-rag]
  ./create_new_project.sh <project_name_or_path> --template rag_app
  ./create_new_project.sh <project_name_or_path> --template dynamic-api
  ./create_new_project.sh <project_name_or_path> --template dynamic-api-vue
  ./create_new_project.sh <project_name_or_path> --template dynamic-api-react
  ./create_new_project.sh <project_name_or_path> --template dynamic-api-angular

Examples:
  ./create_new_project.sh ../my_api
  ./create_new_project.sh my_api
  ./create_new_project.sh ../my_api --with-dynamic-api-vue
  ./create_new_project.sh ../my_api --with-dynamic-api-react
  ./create_new_project.sh ../my_api --with-dynamic-api-angular

The generated application links to qornix::web_core and does not copy framework sources.
Use --with-dynamic-api to generate a Schema-driven Dynamic API application.
Use --with-dynamic-api-vue to generate a Schema-driven Dynamic API application with a Vue/Vite frontend scaffold.
Use --with-dynamic-api-react to generate a Schema-driven Dynamic API application with a React/Vite frontend scaffold.
Use --with-dynamic-api-angular to generate a Schema-driven Dynamic API application with an Angular frontend scaffold.
Use --template rag_app to generate a Qornix Web application with embedded qornix_rag routes.
Use --with-rag to add embedded qornix_rag routes to the default application template.
USAGE
}

fail() {
    echo "Error: $*" >&2
    exit 1
}

script_dir() {
    local source="${BASH_SOURCE[0]}"
    while [ -h "$source" ]; do
        local dir
        dir="$(cd -P "$(dirname "$source")" >/dev/null 2>&1 && pwd)"
        source="$(readlink "$source")"
        case "$source" in
            /*) ;;
            *) source="$dir/$source" ;;
        esac
    done
    cd -P "$(dirname "$source")" >/dev/null 2>&1 && pwd
}

absolute_path() {
    local path="$1"
    local parent
    local name
    parent="$(dirname "$path")"
    name="$(basename "$path")"
    mkdir -p "$parent"
    parent="$(cd "$parent" >/dev/null 2>&1 && pwd -P)"
    printf '%s/%s\n' "$parent" "$name"
}

relative_path() {
    local from_dir="$1"
    local to_path="$2"

    if command -v python3 >/dev/null 2>&1; then
        python3 -c 'import os, sys; print(os.path.relpath(os.path.abspath(sys.argv[2]), os.path.abspath(sys.argv[1])))' "$from_dir" "$to_path" < /dev/null
    else
        printf '%s\n' "$to_path"
    fi
}
replace_placeholders() {
    local file="$1"
    local project_name="$2"
    local project_name_upper="$3"
    local qornix_web_root="$4"
    local with_rag_cmake_bool="$5"
    local with_rag_project_option="$6"
    local with_rag_compile_def="$7"
    local with_rag_link_lib="$8"
    local with_rag_post_build="$9"
    local with_rag_install="${10}"
    local with_rag_config="${11}"
    local with_rag_readme="${12}"
    local tmp_file
    tmp_file="${file}.tmp"
    if command -v python3 >/dev/null 2>&1; then
        PROJECT_NAME_VALUE="$project_name" \
        PROJECT_NAME_UPPER_VALUE="$project_name_upper" \
        QORNIX_WEB_ROOT_VALUE="$qornix_web_root" \
        WITH_RAG_CMAKE_BOOL_VALUE="$with_rag_cmake_bool" \
        WITH_RAG_PROJECT_OPTION_VALUE="$with_rag_project_option" \
        WITH_RAG_COMPILE_DEF_VALUE="$with_rag_compile_def" \
        WITH_RAG_LINK_LIB_VALUE="$with_rag_link_lib" \
        WITH_RAG_POST_BUILD_VALUE="$with_rag_post_build" \
        WITH_RAG_INSTALL_VALUE="$with_rag_install" \
        WITH_RAG_CONFIG_VALUE="$with_rag_config" \
        WITH_RAG_README_VALUE="$with_rag_readme" \
        python3 - "$file" "$tmp_file" <<'PY'
import os
import sys

src, dst = sys.argv[1], sys.argv[2]
replacements = {
    "@PROJECT_NAME@": os.environ["PROJECT_NAME_VALUE"],
    "@PROJECT_NAME_UPPER@": os.environ["PROJECT_NAME_UPPER_VALUE"],
    "@QORNIX_WEB_ROOT@": os.environ["QORNIX_WEB_ROOT_VALUE"],
    "@WITH_RAG_CMAKE_BOOL@": os.environ["WITH_RAG_CMAKE_BOOL_VALUE"],
    "@WITH_RAG_PROJECT_OPTION@": os.environ["WITH_RAG_PROJECT_OPTION_VALUE"],
    "@WITH_RAG_COMPILE_DEF@": os.environ["WITH_RAG_COMPILE_DEF_VALUE"],
    "@WITH_RAG_LINK_LIB@": os.environ["WITH_RAG_LINK_LIB_VALUE"],
    "@WITH_RAG_POST_BUILD@": os.environ["WITH_RAG_POST_BUILD_VALUE"],
    "@WITH_RAG_INSTALL@": os.environ["WITH_RAG_INSTALL_VALUE"],
    "@WITH_RAG_CONFIG@": os.environ["WITH_RAG_CONFIG_VALUE"],
    "@WITH_RAG_README@": os.environ["WITH_RAG_README_VALUE"],
}
with open(src, "r", encoding="utf-8") as handle:
    content = handle.read()
for token, value in replacements.items():
    content = content.replace(token, value)
with open(dst, "w", encoding="utf-8") as handle:
    handle.write(content)
PY
    else
        sed \
            -e "s|@PROJECT_NAME@|$project_name|g" \
            -e "s|@PROJECT_NAME_UPPER@|$project_name_upper|g" \
            -e "s|@QORNIX_WEB_ROOT@|$qornix_web_root|g" \
            -e "s|@WITH_RAG_CMAKE_BOOL@|$with_rag_cmake_bool|g" \
            -e "s|@WITH_RAG_PROJECT_OPTION@||g" \
            -e "s|@WITH_RAG_COMPILE_DEF@|$with_rag_compile_def|g" \
            -e "s|@WITH_RAG_LINK_LIB@|$with_rag_link_lib|g" \
            -e "s|@WITH_RAG_POST_BUILD@||g" \
            -e "s|@WITH_RAG_INSTALL@||g" \
            -e "s|@WITH_RAG_CONFIG@||g" \
            -e "s|@WITH_RAG_README@||g" \
            "$file" > "$tmp_file"
    fi
    mv "$tmp_file" "$file"
}

project_path="${1:-}"
template_name="app"
with_rag="false"
shift || true
while [ $# -gt 0 ]; do
    case "$1" in
        --with-dynamic-api) template_name="dynamic_api_app" ;;
        --with-dynamic-api-vue) template_name="dynamic_api_vue_app" ;;
        --with-dynamic-api-react) template_name="dynamic_api_react_app" ;;
        --with-dynamic-api-angular) template_name="dynamic_api_angular_app" ;;
        --with-rag) with_rag="true" ;;
        --template)
            shift || fail "--template requires a value"
            case "${1:-}" in
                dynamic-api|dynamic_api|dynamic_api_app) template_name="dynamic_api_app" ;;
                dynamic-api-vue|dynamic_api_vue|dynamic_api_vue_app) template_name="dynamic_api_vue_app" ;;
                dynamic-api-react|dynamic_api_react|dynamic_api_react_app) template_name="dynamic_api_react_app" ;;
                dynamic-api-angular|dynamic_api_angular|dynamic_api_angular_app|angular-dynamic-api) template_name="dynamic_api_angular_app" ;;
                rag-app|rag_app|rag) template_name="rag_app" ;;
                app|default) template_name="app" ;;
                *) fail "Unknown template: $1" ;;
            esac
            ;;
        --template=dynamic-api|--template=dynamic_api|--template=dynamic_api_app) template_name="dynamic_api_app" ;;
        --template=dynamic-api-vue|--template=dynamic_api_vue|--template=dynamic_api_vue_app) template_name="dynamic_api_vue_app" ;;
        --template=dynamic-api-react|--template=dynamic_api_react|--template=dynamic_api_react_app) template_name="dynamic_api_react_app" ;;
        --template=dynamic-api-angular|--template=dynamic_api_angular|--template=dynamic_api_angular_app|--template=angular-dynamic-api) template_name="dynamic_api_angular_app" ;;
        --template=rag-app|--template=rag_app|--template=rag) template_name="rag_app" ;;
        --template=app|--template=default) template_name="app" ;;
        *) fail "Unknown option: $1" ;;
    esac
    shift || true
done

if [ "${project_path}" = "-h" ] || [ "${project_path}" = "--help" ]; then
    print_usage
    exit 0
fi

if [ -z "$project_path" ]; then
    print_usage
    echo ""
    printf 'Project name or path: '
    read -r project_path
fi

[ -n "$project_path" ] || fail "Project name or path is required"

framework_root="$(script_dir)"
template_dir="${framework_root}/templates/${template_name}"
[ -d "$template_dir" ] || fail "Application template not found: $template_dir"

if [ "$with_rag" = "true" ] && [ "$template_name" != "app" ]; then
    fail "--with-rag currently supports the default app template. Use --template rag_app for a dedicated RAG app."
fi

project_abs_path="$(absolute_path "$project_path")"
project_name="$(basename "$project_abs_path")"

case "$project_name" in
    *[!A-Za-z0-9_]*)
        fail "Project name must contain only letters, numbers and underscores: $project_name"
        ;;
esac

case "$project_name" in
    [A-Za-z_]*) ;;
    *) fail "Project name must start with a letter or underscore: $project_name" ;;
esac

if [ -e "$project_abs_path" ]; then
    fail "Target already exists: $project_abs_path"
fi

mkdir -p "$project_abs_path"
cp -R "${template_dir}/." "$project_abs_path/"

if [ "$with_rag" = "true" ]; then
    command -v python3 >/dev/null 2>&1 || fail "--with-rag requires python3 for template generation"
    rag_template_dir="${framework_root}/templates/rag_app"
    [ -d "$rag_template_dir" ] || fail "RAG template assets not found: $rag_template_dir"

    mkdir -p \
        "$project_abs_path/templates" \
        "$project_abs_path/static" \
        "$project_abs_path/doc" \
        "$project_abs_path/knowledge_base" \
        "$project_abs_path/models" \
        "$project_abs_path/data" \
        "$project_abs_path/logs"

    cp "${rag_template_dir}/templates/rag_interface.html" "$project_abs_path/templates/rag_interface.html"
    cp "${rag_template_dir}/static/rag_app.css" "$project_abs_path/static/rag_app.css"
    cp "${rag_template_dir}/doc/rag_app.md" "$project_abs_path/doc/rag_app.md"
    cp "${rag_template_dir}/doc/operations.md" "$project_abs_path/doc/operations.md"
    cp "${rag_template_dir}/knowledge_base/README.md" "$project_abs_path/knowledge_base/README.md"
    cp "${rag_template_dir}/models/README.md" "$project_abs_path/models/README.md"
    cp "${rag_template_dir}/download_onnx_model.sh" "$project_abs_path/download_onnx_model.sh"
    chmod +x "$project_abs_path/download_onnx_model.sh"
fi

qornix_web_root_for_cmake="$(relative_path "$project_abs_path" "$framework_root")"
project_name_upper="$(printf '%s' "$project_name" | tr '[:lower:]' '[:upper:]')"

with_rag_cmake_bool="OFF"
with_rag_project_option=""
with_rag_compile_def=""
with_rag_link_lib=""
with_rag_post_build=""
with_rag_install=""
with_rag_config=""
with_rag_readme=""

if [ "$with_rag" = "true" ]; then
    with_rag_cmake_bool="\${${project_name_upper}_ENABLE_RAG}"
    with_rag_project_option="option(${project_name_upper}_ENABLE_RAG \"Enable embedded qornix_rag routes\" ON)"
    with_rag_compile_def="\$<\$<BOOL:\${${project_name_upper}_ENABLE_RAG}>:${project_name_upper}_ENABLE_RAG=1>"
    with_rag_link_lib="\$<\$<BOOL:\${${project_name_upper}_ENABLE_RAG}>:qornix::rag_extension>"
    with_rag_post_build="    COMMAND \${CMAKE_COMMAND} -E copy_if_different \"\${CMAKE_CURRENT_SOURCE_DIR}/download_onnx_model.sh\" \"\${${project_name_upper}_DEPLOY_DIR}/download_onnx_model.sh\"
    COMMAND \${CMAKE_COMMAND} -E copy_directory \"\${CMAKE_CURRENT_SOURCE_DIR}/knowledge_base\" \"\${${project_name_upper}_DEPLOY_DIR}/knowledge_base\"
    COMMAND \${CMAKE_COMMAND} -E copy_directory \"\${CMAKE_CURRENT_SOURCE_DIR}/models\" \"\${${project_name_upper}_DEPLOY_DIR}/models\"
    COMMAND \${CMAKE_COMMAND} -E make_directory \"\${${project_name_upper}_DEPLOY_DIR}/data\""
    with_rag_install="install(FILES download_onnx_model.sh DESTINATION .)
install(DIRECTORY knowledge_base models DESTINATION .)
install(DIRECTORY DESTINATION data)"
    with_rag_config="
auth:
  enabled: false
  mode: session
  registration_enabled: false
  secure_cookies: false
  auto_migrate: true
  min_password_length: 12
  session_duration_minutes: 30
  jwt_duration_minutes: 60
  jwt_secret: change-this-secret-key-in-production
  jwt_issuer: qornix-auth
  default_roles: user
  default_permissions: rag:read
  required_roles:
  required_permissions:
  admin_permissions: auth:admin
  rag_read_permissions: rag:read
  rag_write_permissions: rag:write
  rag_admin_permissions: rag:admin
  exclude_paths: /auth/login,/auth/register,/health,/static
  database:
    driver: sqlite
    path: data/auth.db
  bootstrap_admin:
    enabled: false
    username: admin
    email:
    password_env: QORNIX_ADMIN_PASSWORD
    roles: admin
    permissions: rag:read,rag:write,rag:admin,auth:admin

rag:
  route:
    ui_path: /rag
    api_prefix: /api/rag

  llm:
    enabled: true
    api_url: http://localhost:11434
    model: llama3.2:3b
    request_timeout_ms: 30000
    temperature: 0.2
    max_tokens: 2048

  embedding:
    backend: tfidf
    enable_fallback: true

  search:
    use_hybrid: true
    top_k: 5
    min_score_threshold: 0.0
    use_query_expansion: true
    use_reranking: true
    rerank_input_multiplier: 3
    rerank_path_boost: 0.15
    rerank_metadata_boost: 0.10
    rerank_exact_content_boost: 0.05

  vector_store:
    backend: local_hnsw
    index_path: data/hnsw_index.bin
    metadata_path: data/hnsw_index.meta.json
    auto_load: true
    auto_save: true

  security:
    enabled: false
    mode: admin_token
    admin_token_env: QORNIX_RAG_ADMIN_TOKEN
    token_header: X-Qornix-RAG-Admin-Token
    role_header: X-Qornix-Role
    admin_role: admin
    protect_admin_routes: true
    protect_write_routes: true

  indexing:
    max_file_size_kb: 1024

  sqlite:
    enabled: true
    db_path: data/rag_kb.db
    source_id: local_qa
    name: Local QA knowledge base
    auto_migrate: true

  markdown:
    enabled: true
    directory_path: knowledge_base
    recursive: true

  cache:
    enabled: true
    backend: memory
    max_size: 1000
    ttl_seconds: 3600

  rate_limit:
    enabled: false

  prompt_cache:
    enabled: true
    max_size: 100
    ttl_seconds: 3600"
    with_rag_readme="
## Optional RAG module

This project was generated with \`--with-rag\`. The normal host application
home page remains at \`/\`, and the embedded RAG UI is mounted separately:

\`\`\`text
http://127.0.0.1:8008/rag
http://127.0.0.1:8008/api/rag/health
\`\`\`

RAG routes:

\`\`\`text
GET  /rag
GET  /api/rag/health
POST /api/rag/ask
POST /api/rag/search
GET  /api/rag/sources
POST /api/rag/index
GET  /api/rag/qa
POST /api/rag/qa
PUT  /api/rag/qa/{id}
DEL  /api/rag/qa/{id}
\`\`\`

The default config uses Ollama at \`http://localhost:11434\` and model
\`llama3.2:3b\`. Change \`rag.llm.model\` in \`config.yaml\`, or set
\`rag.llm.enabled: false\` for search-only mode.

Disable the embedded RAG routes at build time when needed:

\`\`\`bash
cmake -D${project_name_upper}_ENABLE_RAG=OFF ..
\`\`\`

The RAG module stores local QA data in \`data/rag_kb.db\`, reads Markdown files
from \`knowledge_base/\`, serves the RAG UI from \`templates/rag_interface.html\`,
and starts with dependency-light TF-IDF retrieval. Optional ONNX embedding files
can be placed under \`models/\` or downloaded with:

\`\`\`bash
./download_onnx_model.sh
\`\`\`
"
fi

# Rename *.in templates after placeholder replacement.
template_file_list="$(mktemp)"
find "$project_abs_path" -type f -name '*.in' | sort > "$template_file_list"
while IFS= read -r file; do
    replace_placeholders "$file" "$project_name" "$project_name_upper" "$qornix_web_root_for_cmake" "$with_rag_cmake_bool" "$with_rag_project_option" "$with_rag_compile_def" "$with_rag_link_lib" "$with_rag_post_build" "$with_rag_install" "$with_rag_config" "$with_rag_readme"
    mv "$file" "${file%.in}"
done < "$template_file_list"
rm -f "$template_file_list"

# Replace placeholders in regular text files.
all_file_list="$(mktemp)"
find "$project_abs_path" -type f | sort > "$all_file_list"
while IFS= read -r file; do
    case "$file" in
        *.in) continue ;;
    esac
    if grep -q '@PROJECT_NAME@\|@PROJECT_NAME_UPPER@\|@QORNIX_WEB_ROOT@\|@WITH_RAG_' "$file" 2>/dev/null; then
        replace_placeholders "$file" "$project_name" "$project_name_upper" "$qornix_web_root_for_cmake" "$with_rag_cmake_bool" "$with_rag_project_option" "$with_rag_compile_def" "$with_rag_link_lib" "$with_rag_post_build" "$with_rag_install" "$with_rag_config" "$with_rag_readme"
    fi
done < "$all_file_list"
rm -f "$all_file_list"

mkdir -p "$project_abs_path/logs"

echo "Created Qornix Web application: $project_abs_path"
echo "Template: $template_name"
if [ "$with_rag" = "true" ]; then
    echo "RAG: enabled"
fi
echo ""
echo "Next steps:"
echo "  cd $project_abs_path"
echo "  mkdir -p build && cd build"
echo "  cmake .."
echo "  cmake --build ."
echo ""
echo "Development run:"
echo "  ./$(basename "$project_name")"
echo ""
echo "Portable deploy bundle:"
echo "  build/deploy/$(basename "$project_name")"
echo "  cd deploy/$(basename "$project_name") && ./$(basename "$project_name")"
echo ""
echo "Runtime Docker image (from the project root after build):"
echo "  docker build -f runtime-Dockerfile -t $(basename "$project_name"):runtime ."
echo "  docker run --rm -p 8008:8008 $(basename "$project_name"):runtime"
echo ""
echo "Open in browser:"
if [ "$template_name" = "rag_app" ]; then
    echo "  http://127.0.0.1:8008/rag"
    echo "  http://127.0.0.1:8008/api/rag/health"
elif [ "$with_rag" = "true" ]; then
    echo "  http://127.0.0.1:8008/"
    echo "  http://127.0.0.1:8008/docs"
    echo "  http://127.0.0.1:8008/rag"
    echo "  http://127.0.0.1:8008/api/rag/health"
elif [ "$template_name" = "dynamic_api_vue_app" ] || [ "$template_name" = "dynamic_api_react_app" ] || [ "$template_name" = "dynamic_api_angular_app" ]; then
    echo "  http://127.0.0.1:8008/"
    echo "  http://127.0.0.1:8008/backend-admin"
    if [ "$template_name" = "dynamic_api_react_app" ] || [ "$template_name" = "dynamic_api_angular_app" ]; then
        echo "  http://127.0.0.1:8008/project-structure"
    fi
    echo "  http://127.0.0.1:8008/backend/schema-manager"
    echo "  http://127.0.0.1:8008/api/dynamic/openapi.json"
elif [ "$template_name" = "dynamic_api_app" ]; then
    echo "  http://127.0.0.1:8008/"
    echo "  http://127.0.0.1:8008/schema-manager"
    echo "  http://127.0.0.1:8008/docs"
else
    echo "  http://127.0.0.1:8008/"
    echo "  http://127.0.0.1:8008/docs"
fi
