# Dynamic Entity Server

## Description

Dynamic Entity Server is an example web server that uses `EntityAPIController` from `qornix_orm` to handle HTTP requests dynamically for different database entities.

## Features

- **Dynamic routing**: the server automatically handles requests for any entities defined in the database schema.
- **Full REST API support**: GET, POST, PUT, PATCH and DELETE methods.
- **Automatic data conversion**: JSON ↔ database objects.
- **Filtering and sorting**: query-parameter support for filtering and sorting result sets.

## API structure

### Base endpoints

```text
GET    /api/{entity}          - Get the list of all entities
GET    /api/{entity}/{id}     - Get an entity by ID
POST   /api/{entity}          - Create a new entity
PUT    /api/{entity}/{id}     - Fully update an entity
PATCH  /api/{entity}/{id}     - Partially update an entity
DELETE /api/{entity}/{id}     - Delete an entity
```

### Usage examples

#### Get an entity list

```bash
curl http://localhost:8008/api/products
```

#### Create a new entity

```bash
curl -X POST http://localhost:8008/api/products \
  -H "Content-Type: application/json" \
  -d '{"name":"Laptop","price":999.99}'
```

#### Get an entity by ID

```bash
curl http://localhost:8008/api/products/1
```

#### Update an entity

```bash
curl -X PUT http://localhost:8008/api/products/1 \
  -H "Content-Type: application/json" \
  -d '{"name":"Updated Laptop","price":899.99}'
```

#### Partial update

```bash
curl -X PATCH http://localhost:8008/api/products/1 \
  -H "Content-Type: application/json" \
  -d '{"price":799.99}'
```

#### Delete an entity

```bash
curl -X DELETE http://localhost:8008/api/products/1
```

## Query parameters

### Filtering

- `{field}={value}` - exact match
- `{field}__gt={value}` - greater than
- `{field}__gte={value}` - greater than or equal
- `{field}__lt={value}` - less than
- `{field}__lte={value}` - less than or equal

### Sorting

- `order_by={field}` - ascending order
- `order_by=-{field}` - descending order

### Pagination

- `limit={number}` - limit the number of results
- `offset={number}` - result offset

## Build and run

### Requirements

- C++20 compiler (g++ or clang++)
- Boost libraries (`url`, `json`)
- PostgreSQL client libraries
- SQLite3 libraries

### Build

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

### Run

```bash
./dynamic_entity_server
```

The server will be available at `http://localhost:8008`.

## Configuration

The server uses the `config.yaml` file from the example directory:

```yaml
server:
  address: 127.0.0.1
  port: 8008
```

Adjust the database and server settings there as needed.

## Project structure

```text
dynamic_entity_server/
├── CMakeLists.txt          # Build configuration
├── main.cpp                # Main server file
├── README.md               # Documentation
└── build/                  # Build directory, created during compilation
```

## Implementation notes

- Uses `EntityAPIController` to handle all CRUD operations.
- Automatically discovers entities from the database schema.
- Supports relationships between entities through foreign keys.
- All operations return JSON data.
- Built-in error handling and validation mechanisms are included.

## Example entities

After the server starts, all entities defined in your database become available. For example:

- `/api/categories` - product categories
- `/api/products` - products
- `/api/orders` - orders
- `/api/users` - users, if present

Each entity supports the full REST operation set according to its schema.

## Adding the example to the framework build

### 1. Place the example in the repository

```text
qornix_web/example/dynamic_entity_server
```

### 2. Ensure the example has its own CMakeLists.txt

The example target should link the framework core and the ORM module as required by the selected database drivers.

### 3. Update the main CMakeLists.txt

Add this to the end of `qornix_web/CMakeLists.txt`:

```cmake
# Add examples
if(QORNIX_BUILD_EXAMPLES)
    add_subdirectory(example/dynamic_entity_server)
endif()
```

### 4. Build and run

```bash
mkdir -p build
cd build
cmake .. -DQORNIX_BUILD_EXAMPLES=ON
cmake --build . --target dynamic_entity_server
./example/dynamic_entity_server/dynamic_entity_server
```

## Summary

This example demonstrates how to use `EntityAPIController` to create a dynamic web server that can automatically process requests to any database entities without writing entity-specific code for each entity.
