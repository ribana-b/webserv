/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Config.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ribana-b <ribana-b@student.42malaga.com>   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/07/05 21:51:15 by ribana-b          #+#    #+# Malaga      */
/*   Updated: 2025/07/18 11:20:46 by ribana-b         ###   ########.com      */
/*                                                                            */
/* ************************************************************************** */

#include "Config.hpp"

#include <fstream>
#include <iostream>
#include <string>

#include "Logger.hpp"

const std::string Config::defaultConfigFilename = "default.conf";

Config::Config(const Logger& logger) : m_Logger(logger) {}

Config::Config() : m_Logger(std::cout, true) {}

Config::~Config() {}

Config::Config(const Config& that) : m_Logger(that.m_Logger) {}

Config& Config::operator=(const Config& that) {
    if (this != &that) {
    }
    return (*this);
}

bool Config::load(const std::string& configFilename) {
    m_Logger.info() << "hello world";
    std::ifstream file(configFilename.c_str());
    if (!file.is_open()) {
        return (false);
    }

    // TODO(srvariable): Fill config fields

    file.close();
    return (true);
}

bool Config::load() { return (load(Config::defaultConfigFilename)); }
