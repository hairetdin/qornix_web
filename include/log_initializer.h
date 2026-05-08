/*
 * Copyright (c) 2026 https://github.com/hairetdin
 *
 * This file is part of QORNIX project.
 * Licensed under GNU GPL v3.0 (see LICENSE file) or commercial license.
 */

#pragma once
#include <boost/core/null_deleter.hpp>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/sinks/text_file_backend.hpp>

#include <iostream>
#include <fstream>
#include <string>

namespace logging = boost::log;
namespace src = boost::log::sources;
namespace sinks = boost::log::sinks;
namespace keywords = boost::log::keywords;
namespace expressions = boost::log::expressions;

class LogInitializer {
public:
    static void initConsoleLogging() {
        logging::add_common_attributes();

        // Create the backend for the console
        auto backend = boost::make_shared<sinks::text_ostream_backend>();
        backend->add_stream(
            boost::shared_ptr<std::ostream>(&std::clog, boost::null_deleter())
        );

        // Create a synchronous sink
        boost::shared_ptr<sinks::synchronous_sink<sinks::text_ostream_backend>> sink(
            new sinks::synchronous_sink<sinks::text_ostream_backend>(backend)
        );

        // Configure the formatter
        sink->set_formatter(
            expressions::stream
                << "[" << expressions::format_date_time<boost::posix_time::ptime>("TimeStamp", "%Y-%m-%d %H:%M:%S")
                << "] <" << logging::trivial::severity
                << "> " << expressions::message
        );

        logging::core::get()->add_sink(sink);
        BOOST_LOG_TRIVIAL(info) << "Console logging initialized";
    }

    static void initFileLogging(const std::string& filename,
                              size_t rotation_size = 10 * 1024 * 1024, // 10 MB
                              size_t max_files = 5) {
        try {
            // Create the backend for file logging
            auto backend = boost::make_shared<sinks::text_file_backend>(
                keywords::file_name = filename + "_%N.log",
                keywords::rotation_size = rotation_size,
                keywords::auto_flush = true
            );

            // Create a synchronous sink for the file
            boost::shared_ptr<sinks::synchronous_sink<sinks::text_file_backend>> sink(
                new sinks::synchronous_sink<sinks::text_file_backend>(backend)
            );

            // Configure the formatter
            sink->set_formatter(
                expressions::stream
                    << "[" << expressions::format_date_time<boost::posix_time::ptime>("TimeStamp", "%Y-%m-%d %H:%M:%S")
            << "] [" << expressions::attr<boost::log::attributes::current_thread_id::value_type>("ThreadID")
            << "] <" << logging::trivial::severity
            << "> " << expressions::message
            );

            logging::core::get()->add_sink(sink);
            BOOST_LOG_TRIVIAL(info) << "File logging initialized: " << filename;
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "Failed to initialize file logging: " << e.what();
            throw;
        }
    }

    static void setLogLevel(boost::log::trivial::severity_level level) {
        logging::core::get()->set_filter(
            logging::trivial::severity >= level
        );
        BOOST_LOG_TRIVIAL(debug) << "Log level set to: " << static_cast<int>(level);
    }

    static void enableTimestamps() {
        logging::add_common_attributes();
    }

    static void disableLogging() {
        logging::core::get()->remove_all_sinks();
        std::clog << "Logging disabled" << std::endl;
    }
};
