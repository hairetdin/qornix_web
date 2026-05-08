# Route Extensions System

## Overview

The route extensions system allows adding new routes and handlers to the running application without recompiling the main server. Each extension is a dynamically loaded library (.so file) that can register its own routes and handle HTTP requests.

## Directory Structure

```
route_extensions/
├── example/              # Extension directory
│   └── handler.cpp      # Extension implementation
├── CMakeLists.txt       # Build configuration
├── build_route_extensions.sh  # Build script
├── create_route.sh      # Helper script to create new routes
└── README.md           # This documentation
```


## Making Scripts Executable

Before using the scripts, make sure they have executable permissions:

```bash
chmod +x build_route_extensions.sh
chmod +x create_route.sh
```


## Creating a New Route

To create a new route extension, use the provided helper script:

```bash
./create_route.sh <route_name>
```


For example:
```bash
./create_route.sh product
```


This will create:
```
route_extensions/
└── product/
    └── handler.cpp
```


The generated `route_extensions/example/handler.cpp` includes:
- A complete handler class with implementations for GET, POST, PUT, DELETE, and PATCH methods
- An extension class that implements the `ExtensionInterface`
- Automatic route registration for common RESTful endpoints:
    - `/<route_name>s`
    - `/<route_name>s/{id}`
    - `/api/v1/<route_name>s`
    - `/api/v1/<route_name>s/{id}`

## Building Extensions

After creating or modifying extensions, build them using:

```bash
./build_route_extensions.sh
```


This script will:
1. Compile all extensions in the directory
2. Generate `.so` files (e.g., `product_route_extension.so`)
3. Place the compiled libraries in the `route_extensions` directory

## Extension Implementation Details

Each extension consists of a single `handler.cpp` file containing:

1. **Handler Class**: Inherits from `HandlerBase` and implements HTTP methods
2. **Extension Class**: Implements `ExtensionInterface` with methods:
    - `getName()`: Returns extension name
    - `initialize()`: Initializes extension with DI container
    - `registerRoutes()`: Registers routes with the HTTP server
    - `cleanup()`: Cleans up resources
3. **Export Functions**: Required `createExtension()` and `destroyExtension()` functions for dynamic loading

## Using Dependency Injection

Extensions have access to the application's DI container through the [initialize()](file:///qornix_web/include/extension_interface.h#L9-L9) method. Services can be resolved using:

```cpp
auto service = container.resolve<ServiceType>("service_name");
```


Check if a service exists:
```cpp
if (container.hasService("service_name")) {
    // Use the service
}
```


## Path Parameters

Path parameters defined in routes (like `{id}` in `/products/{id}`) are automatically extracted and passed to handlers via the `path_params` map:

```cpp
void handleGet(
    const http::request<http::string_body>& req,
    http::response<http::string_body>& res,
    const urls::url_view& url_view,
    const std::map<std::string, std::string>& path_params
) override {
    auto id_it = path_params.find("id");
    if (id_it != path_params.end()) {
        std::string item_id = id_it->second;
        // Use the extracted parameter
    }
}
```


## Adding Custom Routes

In your extension's [registerRoutes()](file:///qornix_web/include/extension_loader.h#L25-L25) method, you can register additional custom routes:

```cpp
void registerRoutes(HttpServer& server, DIContainer& container) override {
    server.add_route("/custom/path", handler_);
    server.add_route("/another/{param}", handler_);
}
```


## Loading Extensions at Runtime

The main application automatically discovers and loads all compiled extension libraries (`.so` files) in the `route_extensions` directory when starting up. No manual intervention is needed after building the extensions.

## Best Practices

1. Keep each extension focused on a single entity or functionality
2. Use descriptive names for route directories
3. Implement proper error handling in HTTP method handlers
4. Clean up resources in the [cleanup()](file:///qornix_web/include/extension_interface.h#L10-L10) method
5. Test extensions thoroughly before deploying to production

## License

This project is dual-licensed under:

1. **GNU General Public License v3.0** (Open Source)
  - You may use, modify, and distribute this software freely under the terms of the GPL v3.0, provided that you also distribute your modifications under the same license.
  - For the full text of the GPL, visit: [GPLv3](https://www.gnu.org/licenses/gpl-3.0.txt).

2. **Commercial License**
  - If you want to use this software in a proprietary or closed-source project, you need to acquire a commercial license.
  - For commercial licensing inquiries, please contact us via [GitHub](https://github.com/hairetdin).

See the [LICENSE](../LICENSE.txt) file for more details.