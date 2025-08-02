/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   main.cpp                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ribana-b <ribana-b@student.42malaga.com>   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/06/02 11:42:05 by ribana-b          #+#    #+# Malaga      */
/*   Updated: 2025/08/02 20:50:17 by ribana-b         ###   ########.com      */
/*                                                                            */
/* ************************************************************************** */

#include <fstream>   // For std::ofstream
#include <iostream>  // For std::cout, std::ios::app
#include <vector>    // For std::vector

#include "Config.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"

static int execMonitor(std::vector<Config::Server> servers, Logger& logger) {
    Monitor monitor(logger);

    if (monitor.init(servers) < 0) {
        return (-1);
    }
    monitor.beginLoop();
    return (0);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
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

    if (argc == 2) {
        if (!config.load(argv[1])) {
            return (1);
        }
    } else {
        if (!config.load()) {
            return (1);
        }
    }

    execMonitor(config.getServers(), logger);

    file.close();

    return (0);
}
