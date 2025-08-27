/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   main.cpp                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ribana-b <ribana-b@student.42malaga.com>   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/06/02 11:42:05 by ribana-b          #+#    #+# Malaga      */
/*   Updated: 2025/08/05 16:36:29 by ribana-b         ###   ########.com      */
/*                                                                            */
/* ************************************************************************** */

#include <fstream>   // For std::ofstream
#include <iostream>  // For std::cout, std::ios::app
#include <string>    // For std::string
#include <vector>    // For std::vector

#include "Config.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"

static int execMonitor(const Config& config, Logger& logger) {
    Monitor monitor(logger);

    if (monitor.init(config) < 0) {
        return (-1);
    }
    monitor.beginLoop();
    return (0);
}

int main(int argc, char* argv[]) {
    if (argc > 2) {
        std::cerr << "\033[31m[ERROR]\033[0m "
                  << "Usage: " << argv[0] << " [CONFIG_FILE]" << std::endl;
        return (1);
    }

    std::ofstream file("webserv.log", std::ios::app);
    if (!file.is_open()) {
        return (1);
    }
    const bool enableColour = true;
    Logger     logger(
            std::cout,
            enableColour);  // NOTE(srvariable): Using std::cout instead of file to have colours
    Config config(logger);

    if (argc == 1) {
        const char* programName = argv[0];
        if (!config.load(programName)) {
            return (1);
        }
    } else {
        const std::string configFilename = argv[1];
        if (!config.load(configFilename)) {
            return (1);
        }
    }

    execMonitor(config, logger);

    file.close();

    return (0);
}
