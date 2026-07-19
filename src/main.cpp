#include "rimau/core/config.hpp"
#include "rimau/core/logger.hpp"
#include "rimau/core/server.hpp"
#include "rimau/core/version.hpp"
#include "rimau/protocol/protocol.hpp"

#include <csignal>
#include <exception>
#include <filesystem>
#include <iostream>
#include <utility>
#include <vector>
#include <string>

namespace {

void print_help()
{
    std::cout
        << rimau::core::project_name << " " << rimau::core::version << "\n"
        << "\n"
        << "Usage:\n"
        << "  rimau-server [--database path] [--check-config] [--protocols]\n"
        << "  rimau-server [--database path] --set key=value [--set key=value ...]\n"
        << "\n"
        << "Options:\n"
        << "  --database path  Load SQLite config database. Default: " << rimau::core::default_database_path << "\n"
        << "  --set key=value  Write a supported config value to SQLite, then exit.\n"
        << "  --check-config   Initialize, validate, and print SQLite config, then exit.\n"
        << "  --protocols      Print protocol implementation status, then exit.\n"
        << "  --version        Print version, then exit.\n"
        << "  --help           Print this help.\n"
        << "\n"
        << "Supported config keys:\n";

    for (const auto& key : rimau::core::supported_config_keys()) {
        std::cout << "  " << key << "\n";
    }
}

void print_protocols(const rimau::core::ServerConfig& config)
{
    for (const auto& capability : rimau::protocol::protocol_capabilities(config)) {
        const bool partial = !capability.implemented
            && (capability.transport.find("partial") != std::string::npos || capability.notes.find("partial") != std::string::npos);
        std::cout << capability.name << ": "
                  << (capability.implemented ? "implemented" : (partial ? "partial" : "not implemented"))
                  << ", configured=" << (capability.enabled ? "enabled" : "disabled")
                  << ", target_transport=" << capability.transport
                  << " - " << capability.notes << '\n';
    }
}

} // namespace

int main(int argc, char** argv)
{
    std::filesystem::path database_path = rimau::core::default_database_path;
    bool check_config = false;
    bool show_protocols = false;
    std::vector<std::pair<std::string, std::string>> config_updates;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];

        if (argument == "--help") {
            print_help();
            return 0;
        }
        if (argument == "--version") {
            std::cout << rimau::core::project_name << " " << rimau::core::version << '\n';
            return 0;
        }
        if (argument == "--protocols") {
            show_protocols = true;
            continue;
        }
        if (argument == "--check-config") {
            check_config = true;
            continue;
        }
        if (argument == "--database") {
            if (index + 1 >= argc) {
                std::cerr << "--database requires a path\n";
                return 2;
            }
            database_path = argv[++index];
            continue;
        }
        if (argument == "--config") {
            std::cerr << "--config has been removed. Use --database with a SQLite database.\n";
            return 2;
        }
        if (argument == "--set") {
            if (index + 1 >= argc) {
                std::cerr << "--set requires key=value\n";
                return 2;
            }

            const std::string update = argv[++index];
            const auto separator = update.find('=');
            if (separator == std::string::npos || separator == 0) {
                std::cerr << "--set requires key=value\n";
                return 2;
            }

            config_updates.emplace_back(update.substr(0, separator), update.substr(separator + 1));
            continue;
        }

        std::cerr << "unknown argument: " << argument << "\n";
        print_help();
        return 2;
    }

#if defined(SIGPIPE)
    std::signal(SIGPIPE, SIG_IGN);
#endif

    try {
        for (const auto& [key, value] : config_updates) {
            rimau::core::set_config_value(database_path, key, value);
        }

        const auto config = rimau::core::load_config_from_database(database_path);

        if (!config_updates.empty()) {
            if (show_protocols) {
                print_protocols(config);
                return 0;
            }
            std::cout << rimau::core::describe_config(config) << '\n';
            return 0;
        }

        if (show_protocols) {
            print_protocols(config);
            return 0;
        }

        if (check_config) {
            std::cout << rimau::core::describe_config(config) << '\n';
            return 0;
        }

        rimau::core::Server server(config);
        return server.run();
    } catch (const std::exception& error) {
        rimau::core::log(rimau::core::LogLevel::error, error.what());
        return 1;
    }
}
