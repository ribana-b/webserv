/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Monitor.hpp                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: disantam <disantam@student.42malaga.com>   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/07/15 13:37:28 by disantam          #+#    #+#             */
/*   Updated: 2025/08/02 20:46:26 by ribana-b         ###   ########.com      */
/*                                                                            */
/* ************************************************************************** */

#ifndef MONITOR_HPP
#define MONITOR_HPP

#define POLLFD_SIZE    10
#define LISTEN_BACKLOG 10
#define POLL_WAIT      30000
#define BUFFER_SIZE    500

/* @------------------------------------------------------------------------@ */
/* |                            Include Section                             | */
/* @------------------------------------------------------------------------@ */

#include <vector>  // For std::vector

#include "Config.hpp"
#include "Logger.hpp"
#include "HttpServer.hpp"

/* @------------------------------------------------------------------------@ */
/* |                             Class Section                              | */
/* @------------------------------------------------------------------------@ */

struct pollfd;

class Monitor {
private:
    Logger                       logger;
    HttpServer                  *httpServer;
    Config                       config;
    std::vector<Config::Server>  servers;  // Store servers for HTTP processing
    struct pollfd               *fds;
    int           *listenFds;
    int           *listenPorts;  // Track which port each listen fd is for
    int           *connectionPorts;  // Track which port each connection fd came from
    int            listenCount;
    int            fdCount;
    int            maxFd;

    enum InitResult { INIT_SUCCESS, INIT_MEMORY_ERROR, INIT_LISTEN_ERROR };

    enum ExecResult { EXEC_SUCCESS, EXEC_CONNECTION_ERROR, EXEC_FATAL_ERROR };

    void       addPollFd(int fdesc);
    void       addPollFd(int fdesc, int port);
    void       closePollFd(int fdesc);
    void       cleanPollFds();
    int        isPollFd(int fdesc) const;
    int        isListenFd(int fdesc) const;
    int        getPortForFd(int fdesc) const;
    int        getPortForConnection(int fdesc) const;
    InitResult initData(std::vector<Config::Server> servers);
    static int initListenFd(struct sockaddr_in &address);
    int        eventInit(int ready);
    int        eventExec(int fdesc, int &ready);
    ExecResult eventExecType(int fdesc, int &ready);
    ExecResult eventExecConnection(int fdesc, int &ready);
    ExecResult eventExecRequest(int fdesc, int &ready);
    ExecResult handleLargeUpload(int fdesc, const std::string& rawRequest, std::size_t headerEnd, std::size_t contentLength, int &ready);

public:
    Monitor(const Logger &logger);
    Monitor();
    ~Monitor();

    int  init(const Config& config);
    void beginLoop();
};

/* @------------------------------------------------------------------------@ */
/* |                            Function Section                            | */
/* @------------------------------------------------------------------------@ */

#endif
