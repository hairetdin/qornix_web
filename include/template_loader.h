/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#ifndef TEMPLATE_LOADER_H
#define TEMPLATE_LOADER_H

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>

class TemplateLoader {
public:
    static std::string loadFile(const std::string& templateName, const std::string& templateDir = "templates") {
        std::string fullPath = templateDir + "/" + templateName;

        // Check whether the file exists
        if (!std::filesystem::exists(fullPath)) {
            std::cerr << "Warning: Template file not found: " << fullPath << std::endl;
            // Try with canonical path
            try {
                std::filesystem::path p(fullPath);
                if (p.is_absolute() || templateDir[0] == '/') {
                    std::cerr << "Warning: Absolute path check failed" << std::endl;
                }
            } catch (...) {}
            return getDefault404Template();
        }

        try {
            std::ifstream file(fullPath);
            if (!file.is_open()) {
                std::cerr << "Warning: Cannot open template file: " << fullPath << std::endl;
                return getDefault404Template();
            }

            std::stringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        } catch (const std::exception& e) {
            std::cerr << "Error loading template " << templateName << ": " << e.what() << std::endl;
            return getDefault404Template();
        }
    }

    static std::string load404Template(const std::string& templateDir = "templates") {
        return loadFile("404.html", templateDir);
    }

    static std::string load500Template(const std::string& templateDir = "templates") {
        return loadFile("500.html", templateDir);
    }

    static bool templateExists(const std::string& templateName, const std::string& templateDir = "templates") {
        std::string fullPath = templateDir + "/" + templateName;
        return std::filesystem::exists(fullPath);
    }

private:
    static std::string getDefault404Template() {
        return R"(
        <!DOCTYPE html>
        <html lang="ru">
        <head>
            <meta charset="UTF-8">
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
            <title>Страница не найдена - 404</title>
            <link rel="icon" type="image/svg+xml" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'%3E%3Ctext y='.9em' font-size='90' font-family='Arial, sans-serif'%3E🦅%3C/text%3E%3C/svg%3E">
            <style>
                body {
                    font-family: Arial, sans-serif;
                    margin: 40px;
                    background: #f5f5f5;
                }
                .container {
                    max-width: 800px;
                    margin: 0 auto;
                    background: white;
                    padding: 30px;
                    border-radius: 10px;
                    box-shadow: 0 0 20px rgba(0,0,0,0.1);
                }
                h1 {
                    color: #2c3e50;
                    text-align: center;
                }
                .error-section {
                    text-align: center;
                    padding: 40px 20px;
                }
                .error-code {
                    font-size: 80px;
                    color: #e74c3c;
                    margin: 20px 0;
                    font-weight: bold;
                }
                .error-title {
                    font-size: 32px;
                    color: #2c3e50;
                    margin: 20px 0;
                }
                .error-message {
                    font-size: 18px;
                    color: #7f8c8d;
                    margin: 20px 0 30px;
                    line-height: 1.6;
                }
                .home-link {
                    display: inline-block;
                    background: #3498db;
                    color: white;
                    padding: 12px 25px;
                    text-decoration: none;
                    border-radius: 5px;
                    font-weight: 500;
                    transition: background 0.3s ease;
                }
                .home-link:hover {
                    background: #2980b9;
                }
                .emoji {
                    font-size: 60px;
                    margin-bottom: 20px;
                }
            </style>
        </head>
        <body>
        <div class="container">
            <div class="error-section">
                <div class="emoji">🔍</div>
                <div class="error-code">404</div>
                <h1 class="error-title">Страница не найдена</h1>
                <p class="error-message">
                    Запрашиваемая страница не существует или была перемещена.<br>
                    Проверьте правильность URL или вернитесь на главную страницу.
                </p>
                <a href="/" class="home-link">← Вернуться на главную</a>
            </div>
        </div>
        </body>
        </html>

        )";
    }
};

#endif // TEMPLATE_LOADER_H
