/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   MonitorInit.cpp                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: disantam <disantam@student.42malaga.com>   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/07/16 15:51:32 by disantam          #+#    #+#             */
/*   Updated: 2025/08/02 21:14:10 by ribana-b         ###   ########.com      */
/*                                                                            */
/* ************************************************************************** */

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <iostream>

#include "Config.hpp"
#include "Monitor.hpp"

int Monitor::init(const Config &config) {
    // Store configuration for HttpServer
    this->config = config;
    this->servers = config.getServers();

    logger.info() << "Monitor::init starting with " << servers.size() << " servers";

    // Initialize HttpServer with config and logger
    this->httpServer = new HttpServer(this->config, this->logger);
    if (this->httpServer == NULL) {
        logger.error() << "Fatal Memory Error initializing HttpServer!";
        return -1;
    }

    logger.info() << "Starting port initialization...";
    Monitor::InitResult result = this->initData(this->servers);

    switch (result) {
        case INIT_SUCCESS:
            logger.info() << "Monitor initialization completed successfully";
            return 0;
        case INIT_MEMORY_ERROR:
            logger.error() << "Fatal Memory Error during port initialization!";
            return -1;
        case INIT_LISTEN_ERROR:
            logger.error() << "Could not initialize ports - check logs above for details";
            return -1;
    }

    logger.warn() << "Unexpected return from initData()";
    return -1;
}

Monitor::InitResult Monitor::initData(std::vector<Config::Server> servers) {
    struct sockaddr_in address;

    this->fds = new struct pollfd[POLLFD_SIZE];
    if (this->fds == NULL) {
        logger.error() << "Failed to allocate memory for poll fds";
        return INIT_MEMORY_ERROR;
    }

    this->connectionPorts = new int[POLLFD_SIZE];
    if (this->connectionPorts == NULL) {
        logger.error() << "Failed to allocate memory for connection ports";
        return INIT_MEMORY_ERROR;
    }

    // Initialize connection ports to -1 (unassigned)
    for (int i = 0; i < POLLFD_SIZE; ++i) {
        this->connectionPorts[i] = -1;
    }

    // Count total listen sockets needed
    int n = 0;
    for (std::size_t i = 0; i < servers.size(); ++i) {
        for (std::size_t j = 0; j < servers[i].listens.size(); ++j) {
            ++n;
        }
    }

    this->listenFds = new int[n];
    if (this->listenFds == NULL) {
        logger.error() << "Failed to allocate memory for listen fds";
        return INIT_MEMORY_ERROR;
    }

    this->listenPorts = new int[n];
    if (this->listenPorts == NULL) {
        logger.error() << "Failed to allocate memory for listen ports";
        return INIT_MEMORY_ERROR;
    }

    n = 0;
    for (std::size_t i = 0; i < servers.size(); ++i) {
        for (std::size_t j = 0; j < servers[i].listens.size(); ++j) {
            memset(&address, 0, sizeof(address));
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = servers[i].listens[j].first;
            address.sin_port = servers[i].listens[j].second;

            logger.info() << "Attempting to create listen socket for "
                          << ntohl(address.sin_addr.s_addr) << ":" << ntohs(address.sin_port);

            listenFds[n] = Monitor::initListenFd(address);

            if (listenFds[n] < 0) {
                logger.error() << "Failed to create listen socket " << n << " for "
                               << ntohl(address.sin_addr.s_addr) << ":" << ntohs(address.sin_port);
                logger.error() << "initListenFd returned: " << listenFds[n];
                return INIT_LISTEN_ERROR;
            }

            // Store port for later HTTP server routing
            listenPorts[n] = ntohs(servers[i].listens[j].second);

            logger.info() << "Successfully listening on " << ntohl(address.sin_addr.s_addr) << ":"
                          << ntohs(address.sin_port) << " (fd=" << listenFds[n] << ")";
            ++n;
        }
    }
    this->listenCount = n;
    return INIT_SUCCESS;
}

int Monitor::initListenFd(struct sockaddr_in &address) {
    int listenFd = 0;
    int optVal = 1;

    // Need to access logger from static context - create temp logger for debugging
    Logger tempLogger(std::cout, true);

    listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0) {
        tempLogger.error() << "socket() failed: " << strerror(errno);
        return -1;
    }

    if (setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &optVal, sizeof(optVal)) < 0) {
        tempLogger.error() << "setsockopt(SO_REUSEADDR) failed: " << strerror(errno);
        close(listenFd);
        return -1;
    }

    if (bind(listenFd, reinterpret_cast<struct sockaddr *>(&address), sizeof(address)) < 0) {
        tempLogger.error() << "bind() failed for " << ntohl(address.sin_addr.s_addr) << ":"
                           << ntohs(address.sin_port) << " - " << strerror(errno);
        close(listenFd);
        return -1;
    }

    if (fcntl(listenFd, F_SETFL, O_NONBLOCK) < 0) {
        tempLogger.error() << "fcntl(O_NONBLOCK) failed: " << strerror(errno);
        close(listenFd);
        return -1;  // Changed from return 1 to return -1 for consistency
    }

    if (listen(listenFd, LISTEN_BACKLOG) < 0) {
        tempLogger.error() << "listen() failed: " << strerror(errno);
        close(listenFd);
        return -1;
    }

    tempLogger.info() << "Socket " << listenFd << " successfully listening on "
                      << ntohl(address.sin_addr.s_addr) << ":" << ntohs(address.sin_port);

    return listenFd;
}
