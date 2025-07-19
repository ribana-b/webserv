/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   MonitorEvent.cpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: disantam <disantam@student.42malaga.com>   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/07/16 15:59:23 by disantam          #+#    #+#             */
/*   Updated: 2025/07/16 19:19:10 by disantam         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include <fcntl.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstddef>
#include <iostream>
#include <string>

#include "Monitor.hpp"

int Monitor::eventInit(int ready) {
    for (int i = 0; ready > 0 && i <= this->maxFd; i++) {
        if (this->isPollFd(i) == 0) {
            continue;
        }
        if (this->eventExec(i, ready) < 0) {
            return -1;
        }
    }
    return 0;
}

int Monitor::eventExec(const int fdesc, int &ready) {
    switch (this->eventExecType(fdesc, ready)) {
        case Monitor::EXEC_SUCCESS:
            return 0;
        case Monitor::EXEC_CONNECTION_ERROR:
            std::cout << "Unable to accept new connection" << '\n';
            return 0;
        case Monitor::EXEC_FATAL_ERROR:
            std::cout << "Fatal error: Aborting!" << '\n';
            return -1;
    }
}

Monitor::ExecResult Monitor::eventExecType(const int fdesc, int &ready) {
    if (this->isListenFd(fdesc) == 1) {
        return this->eventExecConnection(fdesc, ready);
    }
    return this->eventExecRequest(fdesc, ready);
}

Monitor::ExecResult Monitor::eventExecConnection(const int fdesc, int &ready) {
    int newFd = 0;
    int accepted = 0;

    while (true) {
        newFd = accept(fdesc, NULL, NULL);
        if (newFd < 0 && errno != EWOULDBLOCK) {
            return Monitor::EXEC_CONNECTION_ERROR;
        }
        if (newFd < 0) {
            break;
        }
        if (fcntl(newFd, F_SETFL, O_NONBLOCK) < 0) {
            return Monitor::EXEC_FATAL_ERROR;
        }
        if (accepted == 0) {
            ready--;
            accepted = 1;
        }
        this->addPollFd(newFd);
    }
    return Monitor::EXEC_SUCCESS;
}

Monitor::ExecResult Monitor::eventExecRequest(const int fdesc, int &ready) {
    ssize_t     bytesRead = 1;
    char        buffer[BUFFER_SIZE + 1];
    std::string request;

    while (bytesRead != 0) {
        bytesRead = recv(fdesc, buffer, BUFFER_SIZE, 0);
        if (bytesRead < 0) {
            break;
        }
        buffer[bytesRead] = '\0';
        request += buffer;
    }
    send(fdesc, request.c_str(), request.size(), 0);
    ready--;
    this->closePollFd(fdesc);
    return Monitor::EXEC_SUCCESS;
}
