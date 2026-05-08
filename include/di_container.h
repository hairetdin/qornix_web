/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once
#include <memory>
#include <map>
#include <functional>
#include <string>
#include <stdexcept>
#include <mutex>

// Lifetime management enum
enum class Lifetime {
    TRANSIENT,    // New instance every time
    SINGLETON,    // One instance for container lifetime
    SCOPED        // One instance per scope (acts as TRANSIENT in this implementation)
};

class DIContainer {
private:
    struct ServiceRegistration {
        std::function<std::shared_ptr<void>()> factory;
        Lifetime lifetime;
        mutable std::shared_ptr<void> instance;
        mutable std::mutex instance_mutex;

        // Custom constructor to handle non-copyable mutex
        ServiceRegistration(
            std::function<std::shared_ptr<void>()> f,
            Lifetime l
        ) : factory(std::move(f)), lifetime(l), instance(nullptr) {}

        // Delete copy constructor and assignment operator due to mutex
        ServiceRegistration(const ServiceRegistration&) = delete;
        ServiceRegistration& operator=(const ServiceRegistration&) = delete;

        // Allow move operations
        ServiceRegistration(ServiceRegistration&&) = default;
        ServiceRegistration& operator=(ServiceRegistration&&) = default;
    };

    std::map<std::string, ServiceRegistration> registrations_;

public:
    // Register type with specified lifetime
    template<typename T>
    void registerType(const std::string& name,
                     std::function<std::shared_ptr<T>()> factory,
                     Lifetime lifetime = Lifetime::TRANSIENT) {
        // Use emplace to construct ServiceRegistration in-place
        registrations_.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(name),
            std::forward_as_tuple(
                [factory]() -> std::shared_ptr<void> {
                    return std::static_pointer_cast<void>(factory());
                },
                lifetime
            )
        );
    }

    // Resolve dependency by name
    template<typename T>
    std::shared_ptr<T> resolve(const std::string& name) {
        auto it = registrations_.find(name);
        if (it != registrations_.end()) {
            auto& registration = it->second;

            switch (registration.lifetime) {
                case Lifetime::TRANSIENT:
                    // Always create new instance
                    return std::static_pointer_cast<T>(registration.factory());

                case Lifetime::SINGLETON:
                    // Create single instance and cache it
                    {
                        std::lock_guard<std::mutex> lock(registration.instance_mutex);
                        if (!registration.instance) {
                            registration.instance = registration.factory();
                        }
                        return std::static_pointer_cast<T>(registration.instance);
                    }

                case Lifetime::SCOPED:
                    // For simplicity, acts as TRANSIENT
                    // Could be extended for true scoped lifetime
                    return std::static_pointer_cast<T>(registration.factory());
            }
        }
        throw std::runtime_error("Service not found: " + name);
    }

    // Check if service exists
    bool hasService(const std::string& name) const {
        return registrations_.find(name) != registrations_.end();
    }

    // Remove service from container
    void removeService(const std::string& name) {
        registrations_.erase(name);
    }
};
