# Qornix RAG Tests

## Структура тестов

```
tests/
├── CMakeLists.txt                    # CMake конфигурация
├── test_llm_client.cpp              # Тесты LLM Client
├── test_rag_api.cpp                 # Тесты API endpoints
├── test_graceful_degradation.cpp    # Тесты graceful degradation
├── test_health_check.cpp            # Тесты health check
├── test_streaming.cpp               # Тесты streaming
├── test_prompt_builder.cpp          # Тесты prompt builder
└── mocks/
    ├── mock_llm_server.h            # Mock LLM server
    └── test_helpers.h               # Тестовые хелперы
```

## Запуск тестов

### Сборка с тестами

```bash
cd qornix_rag
mkdir -p build && cd build
cmake .. -DQORNIX_BUILD_TESTS=ON
make -j$(nproc)
```

### Запуск отдельных тестов

```bash
# Все тесты
ctest --output-on-failure

# Конкретный тест
./tests/test_llm_client
./tests/test_rag_api
./tests/test_graceful_degradation
./tests/test_health_check
./tests/test_streaming
./tests/test_prompt_builder
```

### Запуск с valgrind (memory leak check)

```bash
valgrind --leak-check=full --error-exitcode=1 ./tests/test_llm_client
```

## Типы тестов

### 1. Unit Tests
- Тестирование отдельных функций
- Mock dependencies
- Быстрые и изолированные

### 2. Integration Tests
- Тестирование взаимодействия компонентов
- Тестирование API endpoints
- Тестирование graceful degradation

### 3. Performance Tests
- Тестирование производительности
- Stress testing
- Memory profiling

## Coverage

Для получения coverage отчёта:

```bash
# Сборка с coverage
cmake .. -DCMAKE_CXX_FLAGS="--coverage"
make -j$(nproc)

# Запуск тестов
ctest

# Генерация отчёта
gcovr --html-details --output coverage.html
```

## CI/CD

Тесты автоматически запускаются в GitHub Actions:

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

## Написание новых тестов

### Шаблон теста

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

### Best practices

1. **Изолированность** — каждый тест должен работать независимо
2. **Чистота** — не оставлять после себя файлы/ресурсы
3. **Скорость** — тесты должны быть быстрыми (< 1 сек)
4. **Понятность** — имена тестов должны быть описательными
5. **Покрытие** — тестировать happy path и error cases

## Troubleshooting

### Тесты не собираются

```bash
# Очистить кэш
rm -rf build
mkdir build && cd build
cmake ..
```

### Тесты падают

```bash
# Запустить с подробным выводом
ctest --output-on-failure --verbose

# Запустить конкретный тест
./tests/test_name
```

### Memory leaks

```bash
# Проверить с valgrind
valgrind --leak-check=full ./tests/test_name
```
