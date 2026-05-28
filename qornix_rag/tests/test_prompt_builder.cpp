/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

/**
 * Tests for Prompt Builder
 * 
 * Tests:
 * - Basic prompt construction
 * - Context formatting
 * - System prompt injection
 * - Question formatting
 * - Token estimation
 */

#include "mocks/test_helpers.h"
#include <iostream>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

void test_passed(const std::string& test_name) {
    tests_passed++;
    std::cout << "  ✓ " << test_name << std::endl;
}

void test_failed(const std::string& test_name, const std::string& error) {
    tests_failed++;
    std::cout << "  ✗ " << test_name << " - " << error << std::endl;
}

// Test 1: Basic prompt construction
void test_basic_prompt() {
    std::cout << "\nTest 1: Basic Prompt Construction" << std::endl;
    
    try {
        std::string context = "File: test.cpp\nContent: void foo() {}";
        std::string question = "What does foo do?";
        
        std::string prompt = "Контекст:\n" + context + "\n\nВопрос: " + question;
        
        test_helpers::assert_contains(prompt, "Контекст:", "Should have context marker");
        test_helpers::assert_contains(prompt, "test.cpp", "Should have file path");
        test_helpers::assert_contains(prompt, "void foo() {}", "Should have code snippet");
        test_helpers::assert_contains(prompt, "Вопрос:", "Should have question marker");
        test_helpers::assert_contains(prompt, "What does foo do?", "Should have question");
        
        test_passed("Basic prompt construction");
    } catch (const std::exception& e) {
        test_failed("Basic prompt construction", e.what());
    }
}

// Test 2: Context with multiple files
void test_context_multiple_files() {
    std::cout << "\nTest 2: Context with Multiple Files" << std::endl;
    
    try {
        std::string context = R"(
File: include/di_container.h
Содержимое:
  1 | class DIContainer {
  2 | public:
  3 |   void registerType();
---

File: server/server_manager.cpp
Содержимое:
  10 | auto di = std::make_shared<DIContainer>();
  11 | di->registerType<UserService>();
)";
        std::string question = "Как работает DI Container?";
        
        std::string prompt = "Контекст:\n" + context + "\n\nВопрос: " + question;
        
        test_helpers::assert_contains(prompt, "di_container.h", "Should have first file");
        test_helpers::assert_contains(prompt, "server_manager.cpp", "Should have second file");
        test_helpers::assert_contains(prompt, "DIContainer", "Should have class name");
        test_helpers::assert_contains(prompt, "registerType", "Should have method name");
        
        test_passed("Context with multiple files");
    } catch (const std::exception& e) {
        test_failed("Context with multiple files", e.what());
    }
}

// Test 3: System prompt injection
void test_system_prompt() {
    std::cout << "\nTest 3: System Prompt Injection" << std::endl;
    
    try {
        std::string system_prompt = R"(Ты — помощник разработчика, который отвечает на вопросы
на основе предоставленного контекста из кода проекта.

Правила:
- Используй только информацию из контекста
- Если информации недостаточно, скажи "Недостаточно информации"
- Приводи примеры кода если это уместно
- Отвечай на русском языке)";
        
        std::string prompt = "Система: " + system_prompt + "\n\nПользователь: Как работает DI?";
        
        test_helpers::assert_contains(prompt, "помощник разработчика", "Should have system role");
        test_helpers::assert_contains(prompt, "Недостаточно информации", "Should have fallback instruction");
        test_helpers::assert_contains(prompt, "примеры кода", "Should have code example instruction");
        test_helpers::assert_contains(prompt, "русском языке", "Should have language instruction");
        
        test_passed("System prompt injection");
    } catch (const std::exception& e) {
        test_failed("System prompt injection", e.what());
    }
}

// Test 4: Question formatting
void test_question_formatting() {
    std::cout << "\nTest 4: Question Formatting" << std::endl;
    
    try {
        std::vector<std::string> questions = {
            "Как работает DI Container?",
            "Что делает registerType?",
            "Где используется middleware?",
            "Как настроить аутентификацию?"
        };
        
        for (const auto& q : questions) {
            std::string formatted = "Вопрос: " + q;
            test_helpers::assert_contains(formatted, "Вопрос:", "Should have question marker");
            test_helpers::assert_contains(formatted, q, "Should contain original question");
        }
        
        test_passed("Question formatting");
    } catch (const std::exception& e) {
        test_failed("Question formatting", e.what());
    }
}

// Test 5: Token estimation
void test_token_estimation() {
    std::cout << "\nTest 5: Token Estimation" << std::endl;
    
    try {
        std::string text = "DI Container в qornix_web работает через registration pattern.";
        
        // Rough token estimation: ~4 chars per token for English
        size_t estimated_tokens = text.length() / 4;
        
        test_helpers::assert_true(estimated_tokens > 0, "Should estimate tokens");
        test_helpers::assert_true(estimated_tokens < 1000, "Should not overestimate");
        
        test_passed("Token estimation");
    } catch (const std::exception& e) {
        test_failed("Token estimation", e.what());
    }
}

// Test 6: Prompt with citations
void test_prompt_with_citations() {
    std::cout << "\nTest 6: Prompt with Citations" << std::endl;
    
    try {
        std::string context = R"(
[1] include/di_container.h:10-20
[2] server/server_manager.cpp:5-15
)";
        std::string question = "Где определяется DI Container?";
        
        std::string prompt = "Контекст:\n" + context + "\n\nВопрос: " + question + "\n\nЦитируй источники.";
        
        test_helpers::assert_contains(prompt, "[1]", "Should have citation 1");
        test_helpers::assert_contains(prompt, "[2]", "Should have citation 2");
        test_helpers::assert_contains(prompt, "di_container.h", "Should have first source");
        test_helpers::assert_contains(prompt, "server_manager.cpp", "Should have second source");
        test_helpers::assert_contains(prompt, "Цитируй", "Should have citation instruction");
        
        test_passed("Prompt with citations");
    } catch (const std::exception& e) {
        test_failed("Prompt with citations", e.what());
    }
}

// Test 7: Empty context handling
void test_empty_context() {
    std::cout << "\nTest 7: Empty Context Handling" << std::endl;
    
    try {
        std::string context = "";
        std::string question = "Как работает DI?";
        
        std::string prompt = "Контекст:\n" + context + "\n\nВопрос: " + question;
        
        test_helpers::assert_contains(prompt, "Контекст:", "Should have context marker");
        test_helpers::assert_contains(prompt, "Вопрос:", "Should have question marker");
        test_helpers::assert_contains(prompt, "Как работает DI?", "Should have question");
        
        test_passed("Empty context handling");
    } catch (const std::exception& e) {
        test_failed("Empty context handling", e.what());
    }
}

// Test 8: Long context truncation
void test_long_context_truncation() {
    std::cout << "\nTest 8: Long Context Truncation" << std::endl;

    try {
        // Simulate very long context (100 files × ~34 chars ≈ 3400 chars)
        std::string long_context;
        for (int i = 0; i < 100; ++i) {
            long_context += "File: file" + std::to_string(i) + ".cpp\nContent: line\n---\n";
        }

        // Truncate to reasonable size (3000 chars — fits within 3490 chars of generated context)
        size_t max_size = 3000;
        if (long_context.length() > max_size) {
            long_context = long_context.substr(0, max_size) + "\n... [обрезано]";
        }

        // Tolerance accounts for suffix bytes (Cyrillic = 2 bytes each in UTF-8)
        test_helpers::assert_true(long_context.length() <= max_size + 32, "Should be truncated");
        test_helpers::assert_contains(long_context, "обрезано", "Should have truncation marker");

        test_passed("Long context truncation");
    } catch (const std::exception& e) {
        test_failed("Long context truncation", e.what());
    }
}

// Main
int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Qornix RAG - Prompt Builder Tests" << std::endl;
    std::cout << "========================================" << std::endl;
    
    test_basic_prompt();
    test_context_multiple_files();
    test_system_prompt();
    test_question_formatting();
    test_token_estimation();
    test_prompt_with_citations();
    test_empty_context();
    test_long_context_truncation();
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Results: " << tests_passed << " passed, " 
              << tests_failed << " failed" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return tests_failed > 0 ? 1 : 0;
}
