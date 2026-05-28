/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include "../include/auth_manager.h"
#include <iostream>
#include <string>

void printMenu() {
    std::cout << "\n=== Qornix Authentication System ===\n";
    std::cout << "1. Register\n";
    std::cout << "2. Login\n";
    std::cout << "3. Logout\n";
    std::cout << "4. Check Status\n";
    std::cout << "5. Exit\n";
    std::cout << "Choose option: ";
}

int main() {
    qornix_auth::AuthConfig config;
    config.minPasswordLength = 12;
    qornix_auth::AuthManager authManager(config);

    std::string currentUserId;
    int choice;

    do {
        printMenu();
        std::cin >> choice;
        std::cin.ignore();

        switch (choice) {
            case 1: {
                std::string username, password, email;
                std::cout << "Username: ";
                std::getline(std::cin, username);
                std::cout << "Password: ";
                std::getline(std::cin, password);
                std::cout << "Email (optional): ";
                std::getline(std::cin, email);

                auto result = authManager.registerUser(username, password, email);

                if (result.status == qornix_auth::AuthStatus::SUCCESS) {
                    std::cout << "✓ User registered successfully! ID: "
                              << result.userId << "\n";
                } else {
                    std::cout << "✗ Error: " << result.message << "\n";
                }
                break;
            }

            case 2: {
                std::string username, password;
                std::cout << "Username: ";
                std::getline(std::cin, username);
                std::cout << "Password: ";
                std::getline(std::cin, password);

                auto result = authManager.authenticate(username, password);

                if (result.status == qornix_auth::AuthStatus::SUCCESS) {
                    currentUserId = result.userId;
                    std::cout << "✓ Login successful!\n";
                } else {
                    std::cout << "✗ Error: " << result.message << "\n";
                }
                break;
            }

            case 3: {
                if (!currentUserId.empty()) {
                    auto result = authManager.logout(currentUserId);
                    if (result.status == qornix_auth::AuthStatus::SUCCESS) {
                        currentUserId.clear();
                        std::cout << "✓ Logged out successfully!\n";
                    }
                } else {
                    std::cout << "No user is currently logged in\n";
                }
                break;
            }

            case 4: {
                if (!currentUserId.empty() && authManager.isAuthenticated(currentUserId)) {
                    std::cout << "✓ User is authenticated. ID: " << currentUserId << "\n";

                    auto user = authManager.getUserById(currentUserId);
                    if (user) {
                        std::cout << "Username: " << user->username << "\n";
                        std::cout << "Email: " << user->email << "\n";
                    }
                } else {
                    std::cout << "✗ No active session\n";
                }
                break;
            }

            case 5:
                std::cout << "Exiting...\n";
                break;

            default:
                std::cout << "Invalid option\n";
        }

    } while (choice != 5);

    return 0;
}
