/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   MonitorInit.cpp                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: disantam <disantam@student.42malaga.com>   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/07/16 15:51:32 by disantam          #+#    #+#             */
/*   Updated: 2025/07/16 19:19:23 by disantam         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/poll.h>
#include <sys/socket.h>

#include <cstdlib>
#include <cstring>
#include <iostream>

#include "Monitor.hpp"

int Monitor::init(char **ports, int n) {
    switch (this->initData(ports, n)) {
        case INIT_SUCCESS:
            return 0;
        case INIT_MEMORY_ERROR:
            std::cout << "Fatal Memory Error!" << '\n';
            return -1;
        case INIT_LISTEN_ERROR:
            std::cout << "Could not initialize ports" << '\n';
            return -1;
    }
}

Monitor::InitResult Monitor::initData(char **ports, int n) {
    struct sockaddr_in address;

    this->fds = new struct pollfd[POLLFD_SIZE];
    if (this->fds == NULL) {
        return INIT_MEMORY_ERROR;
    }
    this->listenFds = new int[n];
    if (this->listenFds == NULL) {
        return INIT_MEMORY_ERROR;
    }
    for (int i = 0; i < n; i++) {
        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(atoi(ports[i]));
        listenFds[i] = Monitor::initListenFd(address);
        if (listenFds[i] < 0) {
            return INIT_LISTEN_ERROR;
        }
    }
    this->listenCount = n;
    return INIT_SUCCESS;
}

int Monitor::initListenFd(struct sockaddr_in &address) {
    int listenFd = 0;
    int optVal = 1;

    listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0) {
        return -1;
    }
    if (setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &optVal, sizeof(listenFd)) < 0) {
        return -1;
    }
    if (bind(listenFd, reinterpret_cast<struct sockaddr *>(&address), sizeof(address)) < 0) {
        return -1;
    }
    if (fcntl(listenFd, F_SETFL, O_NONBLOCK) < 0) {
        return 1;
    }
    if (listen(listenFd, LISTEN_BACKLOG) < 0) {
        return -1;
    }
    return listenFd;
}
