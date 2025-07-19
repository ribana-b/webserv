/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Config.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ribana-b <ribana-b@student.42malaga.com>   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/07/05 21:50:25 by ribana-b          #+#    #+# Malaga      */
/*   Updated: 2025/07/19 20:41:46 by ribana-b         ###   ########.com      */
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
    Config(const Logger& logger);
    Config();
    ~Config();
    Config(const Config& that);
    Config& operator=(const Config& that);

    bool load(const std::string& configFilename);
    bool load();

private:
    typedef std::pair<uint32_t, uint16_t> Listen;
    struct Location {
        std::string           path;
        std::string           root;
        std::string           index;
        bool                  autoindex;
        std::set<std::string> allowMethods;
        std::size_t           clientMaxBodySize;
    };

    struct Server {
        std::string                path;
        std::string                root;
        std::string                index;
        std::vector<Listen>        listens;
        std::vector<Location>      locations;
        std::map<int, std::string> errorPages;
    };

    static const std::string defaultConfigFilename;

    Logger              m_Logger;
    std::vector<Server> servers;
};

#endif
