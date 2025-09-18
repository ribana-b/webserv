/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Monitor.cpp                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: disantam <disantam@student.42malaga.com>   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/07/15 13:51:46 by disantam          #+#    #+#             */
/*   Updated: 2025/08/02 20:46:21 by ribana-b         ###   ########.com      */
/*                                                                            */
/* ************************************************************************** */

#include "Monitor.hpp"

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>

#include "UploadManager.hpp"

Monitor::Monitor(const Logger& newLogger) : logger(newLogger), httpServer(NULL) {
    this->fds = NULL;
    this->listenFds = NULL;
    this->listenPorts = NULL;
    this->connectionPorts = NULL;
    this->listenCount = 0;
    this->fdCount = 0;
    this->maxFd = 0;
}

Monitor::Monitor() : httpServer(NULL) {
    this->fds = NULL;
    this->listenFds = NULL;
    this->listenPorts = NULL;
    this->connectionPorts = NULL;
    this->listenCount = 0;
    this->fdCount = 0;
    this->maxFd = 0;
}

Monitor::~Monitor() {
    // Clean up active uploads
    for (std::map<int, UploadState*>::iterator it = activeUploads.begin();
         it != activeUploads.end(); ++it) {
        if (it->second != NULL) {
            it->second->manager->cleanup();
            delete it->second;
        }
    }
    activeUploads.clear();

    delete[] this->fds;
    delete[] this->listenFds;
    delete[] this->listenPorts;
    delete[] this->connectionPorts;
    delete this->httpServer;
}

void Monitor::beginLoop() {
    int ready = 0;

    for (int i = 0; i < this->listenCount; i++) {
        this->addPollFd(this->listenFds[i]);
    }
    while (true) {
        ready = poll(this->fds, this->fdCount, POLL_WAIT);
        if (ready < 0) {
            break;
        }
        if (ready > 0 && this->eventInit(ready) < 0) {
            break;
        }
    }
    this->cleanPollFds();
}

void Monitor::addPollFd(const int fdesc) {
    this->fds[this->fdCount].fd = fdesc;
    this->fds[this->fdCount].events = POLLIN;
    this->connectionPorts[this->fdCount] = -1;  // No port assigned
    this->fdCount++;
    this->maxFd = std::max(fdesc, this->maxFd);
}

void Monitor::addPollFd(const int fdesc, int port) {
    this->fds[this->fdCount].fd = fdesc;
    this->fds[this->fdCount].events = POLLIN;
    this->connectionPorts[this->fdCount] = port;  // Store port for this connection
    this->fdCount++;
    this->maxFd = std::max(fdesc, this->maxFd);
}

void Monitor::closePollFd(const int fdesc) {
    int itr = 0;

    // Clean up any upload state for this file descriptor
    removeUploadState(fdesc);

    close(fdesc);
    while (itr < this->fdCount && fdesc != this->fds[itr].fd) {
        itr++;
    }
    while (itr + 1 < this->fdCount) {
        this->fds[itr].fd = this->fds[itr + 1].fd;
        this->connectionPorts[itr] = this->connectionPorts[itr + 1];  // Keep ports in sync
        itr++;
    }
    this->fdCount--;
}

void Monitor::cleanPollFds() {
    for (int i = 0; i < this->fdCount; i++) {
        close(this->fds[i].fd);
        this->fds[i].fd = -1;
    }
    this->fdCount = 0;
    this->maxFd = -1;
}

int Monitor::isPollFd(const int fdesc) const {
    for (int i = 0; i < this->fdCount; i++) {
        if (fdesc == this->fds[i].fd) {
            return 1;
        }
    }
    return 0;
}

int Monitor::isListenFd(const int fdesc) const {
    for (int i = 0; i < this->listenCount; i++) {
        if (fdesc == listenFds[i]) {
            return 1;
        }
    }
    return 0;
}

int Monitor::getPortForFd(const int fdesc) const {
    for (int i = 0; i < this->listenCount; i++) {
        if (fdesc == listenFds[i]) {
            return listenPorts[i];
        }
    }
    return -1;  // Not found
}

int Monitor::getPortForConnection(const int fdesc) const {
    for (int i = 0; i < this->fdCount; i++) {
        if (fdesc == this->fds[i].fd) {
            return connectionPorts[i];
        }
    }
    return -1;
}
