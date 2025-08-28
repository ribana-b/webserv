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

#define POLLFD_SIZE           10
#define LISTEN_BACKLOG        10
#define POLL_WAIT             30000
#define BUFFER_SIZE           500
#define CONTENT_LENGTH_HEADER 15
#define DEFAULT_SERVER_PORT   8080
#define POLL_TIMEOUT_MS       5000

/* @------------------------------------------------------------------------@ */
/* |                            Include Section                             | */
/* @------------------------------------------------------------------------@ */

#include <vector>  // For std::vector

#include "Config.hpp"
#include "HttpServer.hpp"
#include "Logger.hpp"

class UploadManager;  // Forward declaration
class HttpResponse;   // Forward declaration
class HttpRequest;    // Forward declaration

/* @------------------------------------------------------------------------@ */
/* |                             Class Section                              | */
/* @------------------------------------------------------------------------@ */

struct pollfd;

class Monitor {
private:
    Logger                      logger;
    HttpServer                 *httpServer;
    Config                      config;
    std::vector<Config::Server> servers;  // Store servers for HTTP processing
    struct pollfd              *fds;
    int                        *listenFds;
    int                        *listenPorts;      // Track which port each listen fd is for
    int                        *connectionPorts;  // Track which port each connection fd came from
    int                         listenCount;
    int                         fdCount;
    int                         maxFd;

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
    struct HeaderPosition {
        std::size_t value;
        explicit HeaderPosition(std::size_t pos) : value(pos) {}
    };

    struct ContentLength {
        std::size_t value;
        explicit ContentLength(std::size_t len) : value(len) {}
    };

    struct UploadInfo {
        std::size_t headerEndPos;
        std::size_t totalContentLength;

        UploadInfo(const HeaderPosition &headerPos, const ContentLength &contentLen) :
            headerEndPos(headerPos.value), totalContentLength(contentLen.value) {}
    };

    ExecResult handleLargeUpload(int fdesc, const std::string &rawRequest,
                                 const UploadInfo &uploadInfo, int &ready);

    // Helper methods to reduce cognitive complexity
    std::string        readHttpRequest(int fdesc);
    bool               processContentLength(const std::string &rawRequest, std::size_t headerEndPos,
                                            std::size_t &totalContentLength, std::string &fullRequest, int fdesc);
    ExecResult         processHttpRequest(int fdesc, const std::string &rawRequest, int &ready);
    ExecResult         streamRemainingData(int fdesc, UploadManager &uploadManager,
                                           std::size_t totalReceived, std::size_t totalContentLength);
    static std::size_t extractContentLength(const std::string &rawRequest,
                                            std::size_t        contentLengthPos);
    HttpResponse       generateHttpResponse(const HttpRequest &httpRequest, int fdesc);
    static void        sendHttpResponse(int fdesc, const HttpResponse &httpResponse);

public:
    Monitor(const Logger &logger);
    Monitor();
    ~Monitor();

    int  init(const Config &config);
    void beginLoop();
};

/* @------------------------------------------------------------------------@ */
/* |                            Function Section                            | */
/* @------------------------------------------------------------------------@ */

#endif
