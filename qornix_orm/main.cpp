/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#include <iostream>
#include "core/handler_interface.h"
#include "core/entity_base.h"
#include <filesystem>
#include <random>

#include "core/handler_interface.h"


// Function to generate a random name
std::string generateRandomName(int length = 8) {
    const std::string chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, chars.length() - 1);

    std::stringstream ss;
    for (int i = 0; i < length; ++i) {
        ss << chars[dis(gen)];
    }

    return ss.str();
}

int main() {

//    // Example database initialization
//    auto db = qornix_orm::database::DatabaseInit::init("config.ini");
//    if (db->isConnected()) {
//        std::cout << "Database connected successfully!" << std::endl;
//    }
    // Initialize the Qornix application
    QornixHandler handler;

    // Initialize with config and schema files
    if (!handler.init("config.yaml", "schema/schema_example.xml")) {
        std::cerr << "Failed to initialize application" << std::endl;
        return 1;
    }
    // The application also has access to the database after initialization
    // Check database connection
    if (handler.db->isConnected()) {
        std::cout << "handler database connected successfully!" << std::endl;
    } else {
        std::cerr << "Failed to connect to database" << std::endl;
        return 1;
    }

//    // Create database structure based on schema
//    if (!handler.createDatabaseStructure()) {
//        std::cerr << "Failed to create database structure" << std::endl;
//        return 1;
//    }
//
//    // Access entities through ModelInterface
    auto userModelObj = handler.model->getModel("users");
    if (!userModelObj) {
        std::cerr << "Failed to get User model" << std::endl;
        return 1;
    }

    // Work with the model - setting fields
    try {
        std::string randomName = generateRandomName(10);

        std::cout << "=== Setting user fields ===" << std::endl;
        userModelObj->setAttribute("username", randomName);
        std::cout << "Set username to '" << randomName << "'" << std::endl;

        userModelObj->setAttribute("email", "john.doe@example.com");
        std::cout << "Set email to 'john.doe@example.com'" << std::endl;

        userModelObj->setAttribute("created_at", "2023-01-01 12:00:00");
        std::cout << "Set created_at to '2023-01-01 12:00:00'" << std::endl;
        userModelObj->save();

        // Create a new user record in database
        // std::cout << "=== Creating user record ===" << std::endl;
        // if (userEntity->create()) {
        //     std::cout << "Successfully created user with ID: " << userEntity->getId() << std::endl;
        // } else {
        //     std::cerr << "Failed to create user" << std::endl;
        // }

        // Read user data
        std::cout << "=== Reading user data ===" << std::endl;
        if (userModelObj->objects().get("email=john.doe@example.com")) {
            std::cout << "Retrieved user: " << userModelObj->getAttributeValue("username") << std::endl;
            std::cout << "Email: " << userModelObj->getAttributeValue("email") << std::endl;
        } else {
            std::cout << "Failed to retrieve user" << std::endl;
        }

        // Update user data
//        std::cout << "=== Updating user data ===" << std::endl;
//        userEntity2->setField("email", "new.email@example.com");
//        std::cout << "Updated email field" << std::endl;
//
//        if (userEntity2->update("1")) {
//            std::cout << "Successfully updated user" << std::endl;
//        } else {
//            std::cout << "Failed to update user" << std::endl;
//        }
//
//        // List all users
//        std::cout << "=== Listing all users ===" << std::endl;
//        auto allUsers = userEntity->listAll();
//        std::cout << "Total users: " << allUsers.size() << std::endl;
//        for (const auto &userData: allUsers) {
//            std::cout << "User: " << userData.at("username")
//                      << " Email: " << userData.at("email") << std::endl;
//        }
//
//        // Execute entity functions
//        std::cout << "=== Executing entity functions ===" << std::endl;
//        std::map<std::string, std::string> params = {
//                {"amount",   "100.00"},
//                {"tax_rate", "0.08"}
//        };
//
//        bool result = userEntity->executeEntityFunction("calculate_tax", params);
//        if (result) {
//            std::cout << "Tax calculation function executed successfully" << std::endl;
//        } else {
//            std::cout << "Tax calculation function execution failed" << std::endl;
//        }

    }
    catch (const std::exception &e) {
        std::cerr << "Error working with entity: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
