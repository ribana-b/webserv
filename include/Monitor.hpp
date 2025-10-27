/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Monitor.hpp                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mancorte <mancorte@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/07/15 13:37:28 by disantam          #+#    #+#             */
/*   Updated: 2025/10/26 01:16:54 by mancorte         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef MONITOR_HPP
#define MONITOR_HPP

#define POLLFD_SIZE           200
#define LISTEN_BACKLOG        128
#define POLL_WAIT             30000
#define BUFFER_SIZE           5000
#define CONTENT_LENGTH_HEADER 15
#define DEFAULT_SERVER_PORT   8080
#define POLL_TIMEOUT_MS       5000
#define MAX_URI_LENGTH        2048
#define MAX_QUERY_STRING_LENGTH 4096
#define MAX_HEADERS_SIZE      8192

/* @------------------------------------------------------------------------@ */
/* |                            Include Section                             | */
/* @------------------------------------------------------------------------@ */

#include <map>     // For std::map
#include <vector>  // For std::vector

#include "Config.hpp"
#include "HttpServer.hpp"
#include "Logger.hpp"

class UploadManager;  // Forward declaration
class HttpResponse;   // Forward declaration
class HttpRequest;    // Forward declaration

struct UploadState {
    UploadManager *manager;
    std::size_t    totalReceived;
    std::size_t    totalContentLength;
    std::string    rawRequest;

    // Constructor parameters are logically ordered and unlikely to be swapped
    UploadState(UploadManager *mgr,
                std::size_t    received,  // NOLINT(bugprone-easily-swappable-parameters)
                std::size_t total, const std::string &request) :
        manager(mgr), totalReceived(received), totalContentLength(total), rawRequest(request) {}
};

struct RequestBuffer {
    std::string buffer;  // Accumulated data from multiple recv() calls

    RequestBuffer() : buffer() {}
};

struct PendingResponse {
    // For small responses (<1MB): use stringData
    std::string stringData;

    // For large responses (>=1MB): stream from file
    int         tempFileFd;     // -1 if using stringData, >=0 if using file
    std::string tempFilePath;   // Path to temp file (for cleanup)
    std::size_t totalSize;      // Total response size

    std::size_t bytesSent;      // How many bytes have been sent already

    // Constructor for string-based responses (small)
    PendingResponse(const std::string &responseData) :
        stringData(responseData), tempFileFd(-1), totalSize(responseData.size()), bytesSent(0) {}

    // Constructor for file-based responses (large)
    PendingResponse(int fileFd, const std::string &filePath, std::size_t size) :
        tempFileFd(fileFd), tempFilePath(filePath), totalSize(size), bytesSent(0) {}
};

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
    int                         *listenFds;
    int                         *listenPorts;      // Track which port each listen fd is for
    int                         *connectionPorts;  // Track which port each connection fd came from
    int                          listenCount;
    int                          fdCount;
    int                          maxFd;
    std::map<int, UploadState *> activeUploads;
    std::map<int, RequestBuffer *> requestBuffers;  // Buffer incomplete HTTP requests
    std::map<int, PendingResponse *> pendingResponses;  // Track partial response sends

    enum InitResult { INIT_SUCCESS, INIT_MEMORY_ERROR, INIT_LISTEN_ERROR };

    enum ExecResult { EXEC_SUCCESS, EXEC_CONNECTION_ERROR, EXEC_FATAL_ERROR };

    void       addPollFd(int fdesc);
    void       addPollFd(int fdesc, int port);
    void       closePollFd(int fdesc);
    void       cleanPollFds();
    int        isPollFd(int fdesc) const;
    int        getFdIndex(int fdesc) const;
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
                                           std::size_t &totalReceived, std::size_t totalContentLength);
    static std::size_t extractContentLength(const std::string &rawRequest,
                                            std::size_t        contentLengthPos);
    HttpResponse       generateHttpResponse(const HttpRequest &httpRequest, int fdesc);
    void               sendHttpResponse(int fdesc, const HttpResponse &httpResponse);

    // Upload state management
    UploadState *getUploadState(int fdesc);
    void         addUploadState(int fdesc, UploadState *state);
    void         removeUploadState(int fdesc);
    ExecResult   continueUpload(int fdesc, int &ready);

    // Request buffer management
    RequestBuffer *getRequestBuffer(int fdesc);
    void           addRequestBuffer(int fdesc, RequestBuffer *buffer);
    void           removeRequestBuffer(int fdesc);

    // Pending response management
    PendingResponse *getPendingResponse(int fdesc);
    void             addPendingResponse(int fdesc, PendingResponse *response);
    void             removePendingResponse(int fdesc);
    ExecResult       continueSend(int fdesc);

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
