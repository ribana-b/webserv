/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Config.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ribana-b <ribana-b@student.42malaga.com>   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/07/05 21:51:15 by ribana-b          #+#    #+# Malaga      */
/*   Updated: 2025/08/02 20:12:29 by ribana-b         ###   ########.com      */
/*                                                                            */
/* ************************************************************************** */

#include "Config.hpp"

#include <arpa/inet.h>   // For htonl, htons
#include <netinet/in.h>  // For INADDR_ANY
#include <stdint.h>      // For int types
#include <sys/stat.h>    // For stat

#include <cstdlib>   // For std::atoi
#include <fstream>   // For std::ifstream
#include <iostream>  // For std::cout
#include <string>    // For std::string, getline, std::string::npos
#include <vector>    // For std::vector

#include "Logger.hpp"

#define DECIMAL 10

#define MEGABYTE (int)(1024 * 1024)
#define BYTE 256

#define FIRST_OCTET 24
#define SECOND_OCTET 16
#define THIRD_OCTET 8
#define FOURTH_OCTET 0

const std::string Config::defaultConfigFilename = "default.conf";

Config::Config(const Logger& logger) : m_Logger(logger) {}

Config::Config() : m_Logger(std::cout, true) {}

Config::~Config() {}

Config::Config(const Config& that) : m_Logger(that.m_Logger), m_Servers(that.m_Servers) {}

Config& Config::operator=(const Config& that) {
    if (this != &that) {
        m_Logger = that.m_Logger;
        m_Servers = that.m_Servers;
    }
    return (*this);
}

static uint32_t octetsToIp(std::vector<int> octets) {
    return ((octets[0] << FIRST_OCTET) | (octets[1] << SECOND_OCTET) | (octets[2] << THIRD_OCTET) |
            (octets[3] << FOURTH_OCTET));
}

static std::vector<int> getOctets(const std::string ipStr) {
    std::vector<int>   octets;
    std::istringstream iss(ipStr);
    std::string        octetStr;

    while (getline(iss, octetStr, '.').good()) {
        std::istringstream iss2(octetStr);
        int                octet;
        iss2 >> octet;
        if (octet >= BYTE) {
            throw(std::exception());  // TODO(srvariable): InvalidOctetException
        }
        octets.push_back(octet);
    }
    std::istringstream iss2(octetStr);
    int                octet;
    iss2 >> octet;
    octets.push_back(octet);

    return (octets);
}

// TODO(srvariable): Make a better validation to ensure only valid ips/ports ranges are introduced
Config::Listen Config::parseListen(const std::string& value) {
    if (value.find("-") != std::string::npos) {
        throw(std::exception());  // TODO(srvariable): InvalidListenException
    }

    std::size_t colonPos = value.find(":");
    if (colonPos == std::string::npos) {
        uint16_t port = static_cast<uint16_t>(std::atoi(value.c_str()));
        uint32_t ip = INADDR_ANY;
        return Listen(htonl(ip), htons(port));
    }

    std::string ipStr = value.substr(0, colonPos);
    std::string portStr = value.substr(colonPos + 1);
    uint32_t    ip = octetsToIp(getOctets(ipStr));
    uint16_t    port = std::atoi(portStr.c_str());

    return Listen(htonl(ip), htons(port));
}

static std::string getValue(std::istringstream& iss) {
    std::string value;
    iss >> value;
    if (!value.empty() && value[value.size() - 1] == ';') {
        value.erase(value.size() - 1);
    }

    return (value);
}

static std::vector<std::string> getValues(std::istringstream& iss) {
    std::vector<std::string> values;
    std::string              value;

    while ((iss >> value).good()) {
        values.push_back(value);
    }
    if (!value.empty() && value[value.size() - 1] == ';') {
        value.erase(value.size() - 1);
    }
    values.push_back(value);

    return (values);
}

std::size_t Config::parseClientMaxBodySize(const std::string& value) {
    if (value.empty()) {
        return (0);
    }

    char*       end;
    std::size_t size = std::strtoul(value.c_str(), &end, DECIMAL);
    if (*end == 'm' || *end == 'M') {
        size *= MEGABYTE;
    } else {
        throw(std::exception());  // TODO(srvariable): InvalidSizeException
    }

    return (size);
}

// TODO(srvariable): Test with invalid configs
void Config::parseLine(const std::string& line) {  // NOLINT
    static Server   server;
    static Location currentLocation;
    static bool     inLocation = false;

    std::istringstream iss(line);
    std::string        key;
    iss >> key;

    if (key == "server" || key == "{") {
        if (key == "server") {
            m_Logger.info() << "server context opened";
        }
        return;
    }

    if (key == "}") {
        if (inLocation) {
            m_Logger.info() << "location context closed";
            server.locations.push_back(currentLocation);
            currentLocation = Location();
            inLocation = false;
        } else {
            m_Logger.info() << "server context closed";
            m_Servers.push_back(server);
            server = Server();
        }
    } else if (key == "listen") {
        m_Logger.info() << "listen rule";
        server.listens.push_back(parseListen(getValue(iss)));
    } else if (key == "root") {
        m_Logger.info() << "root rule";
        std::string root = getValue(iss);
        if (inLocation) {
            currentLocation.root = root;
        } else {
            server.root = root;
        }
    } else if (key == "error_page") {
        m_Logger.info() << "error_page rule";
        int errorCode;
        iss >> errorCode;
        server.errorPages[errorCode] = getValue(iss);
    } else if (key == "location") {
        m_Logger.info() << "location context opened";
        inLocation = true;
        std::string path;
        iss >> path;
        currentLocation.path = path;
    } else if (key == "index") {
        m_Logger.info() << "index rule";
        std::vector<std::string> index = getValues(iss);
        if (inLocation) {
            currentLocation.index = index;
        } else {
            server.index = index;
        }
    } else if (key == "autoindex") {
        m_Logger.info() << "autoindex rule";
        std::string value = getValue(iss);
        if (value == "on") {
            currentLocation.autoindex = true;
        } else if (value == "off") {
            currentLocation.autoindex = false;
        } else {
            throw(std::exception());  // TODO(srvariable): InvalidValueException
        }
    } else if (key == "allow_methods") {
        m_Logger.info() << "allow_methods rule";
        std::vector<std::string> values = getValues(iss);
        for (std::size_t i = 0; i < values.size(); ++i) {
            currentLocation.allowMethods.insert(values[i]);
        }
    } else if (key == "client_max_body_size") {
        m_Logger.info() << "client_max_body_size rule";
        currentLocation.clientMaxBodySize = parseClientMaxBodySize(getValue(iss));
    } else {
        m_Logger.warn() << "unknown rule/context: " << key;
    }
}

static void trim(std::string& line) {
    std::size_t start = line.find_first_not_of(" \t\r\v\f");
    if (start == std::string::npos) {
        return;
    }
    std::size_t end = line.find_last_not_of(" \t\r\v\f");
    line = line.substr(start, end - start + 1);
}

static void removeComments(std::string& line) {
    std::size_t commentPosition = line.find("#");
    if (commentPosition != std::string::npos) {
        line = line.substr(0, commentPosition);
    }
}

static bool isDirectory(const std::string& path) {
    struct stat info;
    if (stat(path.c_str(), &info) != 0) {
        return (false);
    }

    return (S_ISDIR(info.st_mode));
}

bool Config::load(const std::string& configFilename) {
    if (isDirectory(configFilename)) {
        m_Logger.error() << "'" << configFilename << "' is not a valid config file";
        return (false);
    }

    std::ifstream file(configFilename.c_str());
    if (!file.is_open()) {
        m_Logger.error() << "Couldn't open '" << configFilename << "'";
        return (false);
    }

    std::string line;

    while (getline(file, line).good()) {
        removeComments(line);
        trim(line);
        if (line.empty()) {
            continue;
        }

        try {
            parseLine(line);
        } catch (const std::exception& e) {
            m_Logger.error() << e.what();
            return (false);
        }
    }

    file.close();
    return (true);
}

bool Config::load() { return (load(Config::defaultConfigFilename)); }

std::vector<Config::Server> Config::getServers() const { return (m_Servers); }
