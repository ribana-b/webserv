/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Monitor.cpp                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: disantam <disantam@student.42malaga.com>   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/07/15 13:51:46 by disantam          #+#    #+#             */
/*   Updated: 2025/07/16 19:18:48 by disantam         ###   ########.fr       */
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

Monitor::Monitor() {
    this->fds = NULL;
    this->listenFds = NULL;
    this->listenCount = 0;
    this->fdCount = 0;
    this->maxFd = 0;
}

Monitor::~Monitor() {
    delete[] this->fds;
    delete[] this->listenFds;
}

void Monitor::beginLoop() {
    int ready = 0;

    for (int i = 0; i < this->listenCount; i++) {
        this->addPollFd(this->listenFds[i]);
    }
    while (true) {
        ready = poll(this->fds, this->fdCount, POLL_WAIT);
        if (ready <= 0) {
            break;
        }
        if (this->eventInit(ready) < 0) {
            break;
        }
    }
    this->cleanPollFds();
}

void Monitor::addPollFd(const int fdesc) {
    this->fds[this->fdCount].fd = fdesc;
    this->fds[this->fdCount].events = POLLIN;
    this->fdCount++;
    this->maxFd = std::max(fdesc, this->maxFd);
}

void Monitor::closePollFd(const int fdesc) {
    int itr = 0;

    close(fdesc);
    while (itr < this->fdCount && fdesc != this->fds[itr].fd) {
        itr++;
    }
    while (itr + 1 < this->fdCount) {
        this->fds[itr].fd = this->fds[itr + 1].fd;
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
