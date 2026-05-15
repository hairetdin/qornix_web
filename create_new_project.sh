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
    local tmp_file
    tmp_file="${file}.tmp"
    sed \
        -e "s|@PROJECT_NAME@|$project_name|g" \
        -e "s|@PROJECT_NAME_UPPER@|$project_name_upper|g" \
        -e "s|@QORNIX_WEB_ROOT@|$qornix_web_root|g" \
        "$file" > "$tmp_file"
    mv "$tmp_file" "$file"
}

project_path="${1:-}"
template_name="app"
shift || true
while [ $# -gt 0 ]; do
    case "$1" in
        --with-dynamic-api) template_name="dynamic_api_app" ;;
        --with-dynamic-api-vue) template_name="dynamic_api_vue_app" ;;
        --with-dynamic-api-react) template_name="dynamic_api_react_app" ;;
        --with-dynamic-api-angular) template_name="dynamic_api_angular_app" ;;
        --template)
            shift || fail "--template requires a value"
            case "${1:-}" in
                dynamic-api|dynamic_api|dynamic_api_app) template_name="dynamic_api_app" ;;
                dynamic-api-vue|dynamic_api_vue|dynamic_api_vue_app) template_name="dynamic_api_vue_app" ;;
                dynamic-api-react|dynamic_api_react|dynamic_api_react_app) template_name="dynamic_api_react_app" ;;
                dynamic-api-angular|dynamic_api_angular|dynamic_api_angular_app|angular-dynamic-api) template_name="dynamic_api_angular_app" ;;
                app|default) template_name="app" ;;
                *) fail "Unknown template: $1" ;;
            esac
            ;;
        --template=dynamic-api|--template=dynamic_api|--template=dynamic_api_app) template_name="dynamic_api_app" ;;
        --template=dynamic-api-vue|--template=dynamic_api_vue|--template=dynamic_api_vue_app) template_name="dynamic_api_vue_app" ;;
        --template=dynamic-api-react|--template=dynamic_api_react|--template=dynamic_api_react_app) template_name="dynamic_api_react_app" ;;
        --template=dynamic-api-angular|--template=dynamic_api_angular|--template=dynamic_api_angular_app|--template=angular-dynamic-api) template_name="dynamic_api_angular_app" ;;
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

qornix_web_root_for_cmake="$(relative_path "$project_abs_path" "$framework_root")"
project_name_upper="$(printf '%s' "$project_name" | tr '[:lower:]' '[:upper:]')"

# Rename *.in templates after placeholder replacement.
template_file_list="$(mktemp)"
find "$project_abs_path" -type f -name '*.in' | sort > "$template_file_list"
while IFS= read -r file; do
    replace_placeholders "$file" "$project_name" "$project_name_upper" "$qornix_web_root_for_cmake"
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
    if grep -q '@PROJECT_NAME@\|@PROJECT_NAME_UPPER@\|@QORNIX_WEB_ROOT@' "$file" 2>/dev/null; then
        replace_placeholders "$file" "$project_name" "$project_name_upper" "$qornix_web_root_for_cmake"
    fi
done < "$all_file_list"
rm -f "$all_file_list"

mkdir -p "$project_abs_path/logs"

echo "Created Qornix Web application: $project_abs_path"
echo "Template: $template_name"
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
echo "  http://127.0.0.1:8008/"
if [ "$template_name" = "dynamic_api_vue_app" ] || [ "$template_name" = "dynamic_api_react_app" ] || [ "$template_name" = "dynamic_api_angular_app" ]; then
    echo "  http://127.0.0.1:8008/backend-admin"
    if [ "$template_name" = "dynamic_api_react_app" ] || [ "$template_name" = "dynamic_api_angular_app" ]; then
        echo "  http://127.0.0.1:8008/project-structure"
    fi
    echo "  http://127.0.0.1:8008/backend/schema-manager"
    echo "  http://127.0.0.1:8008/api/dynamic/openapi.json"
elif [ "$template_name" = "dynamic_api_app" ]; then
    echo "  http://127.0.0.1:8008/schema-manager"
    echo "  http://127.0.0.1:8008/docs"
else
    echo "  http://127.0.0.1:8008/docs"
fi
