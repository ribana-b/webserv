/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Config.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ribana-b <ribana-b@student.42malaga.com>   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/07/05 21:50:25 by ribana-b          #+#    #+# Malaga      */
/*   Updated: 2025/08/02 21:55:39 by ribana-b         ###   ########.com      */
/*                                                                            */
/* ************************************************************************** */

#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <stdint.h>  // For int types

#include <map>      // For std::map
#include <set>      // For std::set
#include <string>   // For std::string
#include <utility>  // For std::pair
#include <vector>   // For std::vector

#include "Logger.hpp"

class Config {
public:
    typedef std::pair<uint32_t, uint16_t> Listen;

    struct Location {
        std::string              path;
        std::string              root;
        std::vector<std::string> index;
        bool                     autoindex;
        std::set<std::string>    allowMethods;
        std::size_t              clientMaxBodySize;
    };

    struct Server {
        std::string                root;
        std::vector<std::string>   index;
        std::vector<Listen>        listens;
        std::vector<Location>      locations;
        std::map<int, std::string> errorPages;
    };

    Config(const Logger& logger);
    Config();
    ~Config();
    Config(const Config& that);
    Config& operator=(const Config& that);

    bool load(const std::string& configFilename);
    bool load();

    std::vector<Server> getServers() const;

private:
    static const std::string defaultConfigFilename;

    Logger              m_Logger;
    std::vector<Server> m_Servers;

    void handleClosedContext(Server& server, Location& currentLocation, bool& inLocation);
    void handleListen(Server& server, std::istringstream& iss);
    void handleRoot(Server& server, Location& currentLocation, bool& inLocation,
                    std::istringstream& iss);
    void handleErrorPage(Server& server, std::istringstream& iss);
    void handleLocation(Location& currentLocation, bool& inLocation, std::istringstream& iss);
    void handleIndex(Server& server, Location& currentLocation, bool& inLocation,
                     std::istringstream& iss);
    void handleAutoindex(Location& currentLocation, std::istringstream& iss);
    void handleAllowMethods(Location& currentLocation, std::istringstream& iss);
    void handleClientMaxBodySize(Location& currentLocation, std::istringstream& iss);

    static Listen      parseListen(const std::string& value);
    static std::size_t parseClientMaxBodySize(const std::string& value);
    void               parseLine(const std::string& line, Server& server, Location& currentLocation,
                                 bool& inLocation);
};

#endif
