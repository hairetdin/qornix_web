# Qornix RAG Tests

## Test Layout

```text
tests/
├── CMakeLists.txt                    # CMake configuration
├── test_llm_client.cpp               # LLM client tests
├── test_rag_api.cpp                  # API endpoint tests
├── test_graceful_degradation.cpp     # Graceful degradation tests
├── test_health_check.cpp             # Health check tests
├── test_streaming.cpp                # Streaming tests
├── test_prompt_builder.cpp           # Prompt builder tests
└── mocks/
    ├── mock_llm_server.h             # Mock LLM server
    └── test_helpers.h                # Test helpers
```

## Running Tests

### Build With Tests

```bash
cd qornix_rag
mkdir -p build && cd build
cmake .. -DQORNIX_BUILD_TESTS=ON
make -j$(nproc)
```

### Run Individual Tests

```bash
# All tests
ctest --output-on-failure

# Specific tests
./tests/test_llm_client
./tests/test_rag_api
./tests/test_graceful_degradation
./tests/test_health_check
./tests/test_streaming
./tests/test_prompt_builder
```

### Run With Valgrind

```bash
valgrind --leak-check=full --error-exitcode=1 ./tests/test_llm_client
```

## Test Types

### 1. Unit Tests

- Test individual functions.
- Use mock dependencies.
- Stay fast and isolated.

### 2. Integration Tests

- Test component interaction.
- Test API endpoints.
- Test graceful degradation behavior.

### 3. Performance Tests

- Test performance-sensitive paths.
- Run stress scenarios.
- Profile memory usage.

## Coverage

To generate a coverage report:

```bash
# Build with coverage
cmake .. -DCMAKE_CXX_FLAGS="--coverage"
make -j$(nproc)

# Run tests
ctest

# Generate the report
gcovr --html-details --output coverage.html
```

## CI/CD

Tests are intended to run automatically in GitHub Actions:

```yaml
name: qornix_rag tests
on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y libcurl4-openssl-dev libxapian-dev libyaml-cpp-dev
      - name: Build
        run: |
          mkdir build && cd build
          cmake .. -DQORNIX_BUILD_TESTS=ON
          make -j$(nproc)
      - name: Run tests
        run: |
          cd build
          ctest --output-on-failure
```

## Writing New Tests

### Test Template

```cpp
void test_example() {
    std::cout << "\nTest: Example Test" << std::endl;

    try {
        // Arrange
        MockLLMClient mock_llm;

        // Act
        std::string result = mock_llm.simulate_request("test");

        // Assert
        test_helpers::assert_contains(result, "expected", "Should contain expected");

        test_passed("Example test");
    } catch (const std::exception& e) {
        test_failed("Example test", e.what());
    }
}
```

### Best Practices

1. **Isolation** - each test should run independently.
2. **Cleanup** - tests should not leave files or resources behind.
3. **Speed** - tests should be fast, preferably under one second.
4. **Clarity** - test names should describe the behavior under test.
5. **Coverage** - test both happy paths and error cases.

## Troubleshooting

### Tests Do Not Build

```bash
# Clear the cache
rm -rf build
mkdir build && cd build
cmake ..
```

### Tests Fail

```bash
# Run with detailed output
ctest --output-on-failure --verbose

# Run a specific test
./tests/test_name
```

### Memory Leaks

```bash
# Check with valgrind
valgrind --leak-check=full ./tests/test_name
```
