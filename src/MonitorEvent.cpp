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

#include <arpa/inet.h>  // For ntohs
#include <errno.h>      // For errno
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>     // For close

#include <cstddef>
#include <cstdio>   // For std::remove
#include <cstring>  // For strerror
#include <iostream>
#include <string>

#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "Monitor.hpp"
#include "UploadManager.hpp"

static std::size_t stringToNumber(const std::string &str) {
    const std::size_t decimal = 10;
    std::size_t       result = 0;
    for (std::size_t i = 0; i < str.length(); ++i) {
        char c = str[i];
        if (c >= '0' && c <= '9') {
            result = result * decimal + (c - '0');
        } else {
            break;
        }
    }
    return result;
}

int Monitor::eventInit(int ready) {
    // Iterate over the fds array, not over fd numbers
    for (int i = 0; ready > 0 && i < this->fdCount; i++) {
        // Check if this fd has any events
        if (fds[i].revents == 0) {
            continue;
        }
        // Process the event for this fd
        if (this->eventExec(fds[i].fd, ready) < 0) {
            return -1;
        }
    }
    return 0;
}

int Monitor::eventExec(const int fdesc, int &ready) {
    ExecResult result = this->eventExecType(fdesc, ready);
    if (result == Monitor::EXEC_FATAL_ERROR) {
        return -1;
    }
    return 0;
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
        // Subject forbids checking errno after I/O operations
        // Simply handle negative return from accept() without errno checking
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
        int listenPort = this->getPortForFd(fdesc);
        this->addPollFd(newFd, listenPort);
    }
    return Monitor::EXEC_SUCCESS;
}

Monitor::ExecResult Monitor::eventExecRequest(const int fdesc, int &ready) {
    // Get the index of this fd in the fds array to access revents
    int fdIndex = getFdIndex(fdesc);
    if (fdIndex == -1) {
        logger.warn() << "eventExecRequest called with unknown fd " << fdesc;
        return Monitor::EXEC_SUCCESS;
    }

    short revents = fds[fdIndex].revents;

    // Log what events we received (for debugging)
    if (revents & POLLHUP) {
        logger.info() << "POLLHUP detected on fd " << fdesc << " - client disconnected";
    }
    if (revents & POLLERR) {
        logger.error() << "POLLERR detected on fd " << fdesc;
    }

    // Check for disconnect/error - clean up and close
    if ((revents & POLLHUP) || (revents & POLLERR)) {
        // Clean up any pending state
        removePendingResponse(fdesc);
        removeUploadState(fdesc);
        removeRequestBuffer(fdesc);
        closePollFd(fdesc);
        ready--;
        return Monitor::EXEC_SUCCESS;
    }

    // Check if this file descriptor has a pending response to send
    PendingResponse *pendingResponse = getPendingResponse(fdesc);
    if (pendingResponse != NULL) {
        // poll() returned - try to send (will get EAGAIN if not ready yet)
        // This is compliant: we wait for poll() to return BEFORE calling send()
        logger.info() << "poll() returned with pending response - attempting send for fd " << fdesc;
        ready--;
        return continueSend(fdesc);
    }

    // Check if this file descriptor has an ongoing upload
    UploadState *uploadState = getUploadState(fdesc);
    if (uploadState != NULL) {
        return continueUpload(fdesc, ready);
    }

    // Normal request processing - only if POLLIN
    if (revents & POLLIN) {
        std::string rawRequest = readHttpRequest(fdesc);

        // If request is incomplete (empty string returned), wait for more data
        if (rawRequest.empty()) {
            ready--;
            return Monitor::EXEC_SUCCESS;
        }

        return processHttpRequest(fdesc, rawRequest, ready);
    }

    // No relevant events, just return
    ready--;
    return Monitor::EXEC_SUCCESS;
}

Monitor::ExecResult Monitor::handleLargeUpload(const int fdesc, const std::string &rawRequest,
                                               const UploadInfo &uploadInfo, int &ready) {
    Logger logger(std::cout, true);

    // Extract boundary from Content-Type header if present
    std::string boundary;
    std::size_t boundaryPos = rawRequest.find("boundary=");
    if (boundaryPos != std::string::npos) {
        std::size_t start = boundaryPos + 9;  // Length of "boundary="
        std::size_t end = rawRequest.find("\r\n", start);
        if (end == std::string::npos) {
            end = rawRequest.find(";", start);
        }
        if (end == std::string::npos) {
            end = rawRequest.length();
        }
        boundary = rawRequest.substr(start, end - start);
        logger.info() << "Extracted multipart boundary: " << boundary;
    }

    // Create UploadManager for streaming
    UploadManager *uploadManager = new UploadManager(logger);
    bool startResult = boundary.empty()
                       ? uploadManager->startLargeUpload(uploadInfo.totalContentLength)
                       : uploadManager->startLargeUpload(uploadInfo.totalContentLength, boundary);

    if (!startResult) {
        logger.error() << "Failed to start large upload streaming";
        delete uploadManager;
        this->closePollFd(fdesc);
        return Monitor::EXEC_SUCCESS;
    }

    // Extract any body data already received
    std::size_t bodyStart = uploadInfo.headerEndPos + 4;
    std::size_t alreadyReceived = 0;
    if (rawRequest.length() > bodyStart) {
        alreadyReceived = rawRequest.length() - bodyStart;
        const char *bodyData = rawRequest.data() + bodyStart;

        if (!uploadManager->writeChunk(bodyData, alreadyReceived)) {
            logger.error() << "Failed to write initial body chunk to disk";
            uploadManager->cleanup();
            delete uploadManager;
            this->closePollFd(fdesc);
            return Monitor::EXEC_SUCCESS;
        }
    }

    // Create upload state and add to tracking
    UploadState *state =
        new UploadState(uploadManager, alreadyReceived, uploadInfo.totalContentLength, rawRequest);
    addUploadState(fdesc, state);

    logger.info() << "Large upload started, received " << alreadyReceived << "/"
                  << uploadInfo.totalContentLength << " bytes initially";

    // Check for Expect: 100-continue header (case insensitive)
    bool        hasExpect100Continue = false;
    std::string lowerRequest = rawRequest;
    for (std::size_t i = 0; i < lowerRequest.length(); ++i) {
        if (lowerRequest[i] >= 'A' && lowerRequest[i] <= 'Z') {
            lowerRequest[i] = lowerRequest[i] + 32;  // To lowercase
        }
    }
    if (lowerRequest.find("expect: 100-continue") != std::string::npos) {
        hasExpect100Continue = true;
    }

    if (hasExpect100Continue) {
        const char *continueResponse = "HTTP/1.1 100 Continue\r\n\r\n";
        send(fdesc, continueResponse, strlen(continueResponse), 0);
        logger.info() << "Sent 100 Continue response to client";
    }

    // Continue reading available data immediately (non-blocking socket)
    char buffer[UPLOAD_BUFFER_SIZE];
    while (state->totalReceived < state->totalContentLength) {
        ssize_t bytesRead = recv(fdesc, buffer, UPLOAD_BUFFER_SIZE, 0);
        if (bytesRead <= 0) {
            // No more data available now, will continue on next poll event
            break;
        }

        std::size_t bytesToWrite = static_cast<std::size_t>(bytesRead);
        if (state->totalReceived + bytesToWrite > state->totalContentLength) {
            bytesToWrite = state->totalContentLength - state->totalReceived;
        }

        if (!uploadManager->writeChunk(buffer, bytesToWrite)) {
            logger.error() << "Failed to write chunk during initial read";
            uploadManager->cleanup();
            removeUploadState(fdesc);
            this->closePollFd(fdesc);
            return Monitor::EXEC_SUCCESS;
        }

        state->totalReceived += bytesToWrite;
    }

    // Check if upload completed immediately
    if (state->totalReceived >= state->totalContentLength || uploadManager->isComplete()) {
        if (!uploadManager->finishUpload()) {
            logger.error() << "Failed to finish large upload";
            uploadManager->cleanup();
            removeUploadState(fdesc);
            this->closePollFd(fdesc);
            return Monitor::EXEC_SUCCESS;
        }

        uploadManager->disableAutoCleanup();

        std::string headersOnly =
            state->rawRequest.substr(0, state->rawRequest.find("\r\n\r\n") + 4);
        logger.info() << "Large upload completed successfully, temp file: "
                      << uploadManager->getTempFilePath();

        HttpRequest httpRequest(logger);
        httpRequest.setTempFilePath(uploadManager->getTempFilePath());
        httpRequest.setOriginalFilename(uploadManager->getOriginalFilename());
        httpRequest.parse(headersOnly);

        HttpResponse httpResponse;
        if (httpRequest.isValid()) {
            int serverPort = this->getPortForConnection(fdesc);
            if (serverPort < 0) {
                if (!this->servers.empty() && !this->servers[0].listens.empty()) {
                    serverPort = this->servers[0].listens[0].second;
                } else {
                    serverPort = DEFAULT_SERVER_PORT;
                }
            }
            httpResponse = this->httpServer->processRequest(httpRequest, serverPort);
        } else {
            logger.warn() << "Invalid HTTP request received (large upload completion path)";
            logger.warn() << "Method: '" << httpRequest.getMethod() << "' Path: '" << httpRequest.getPath()
                         << "' Version: '" << httpRequest.getVersion() << "'";
            logger.warn() << "Headers only (first 200 chars): "
                         << headersOnly.substr(0, std::min(headersOnly.length(), size_t(200)));
            httpResponse = HttpResponse::createBadRequest();
        }

        sendHttpResponse(fdesc, httpResponse);

        removeUploadState(fdesc);

        // Only close connection if response was fully sent
        // If there's a pending response, keep connection open for POLLOUT handling
        if (getPendingResponse(fdesc) == NULL) {
            this->closePollFd(fdesc);
        } else {
            logger.info() << "Response pending on fd " << fdesc << ", keeping connection open for POLLOUT";
        }
    }

    ready--;
    return Monitor::EXEC_SUCCESS;
}

std::string Monitor::readHttpRequest(int fdesc) {
    char    buffer[BUFFER_SIZE + 1];
    ssize_t bytesRead;

    // Get or create request buffer for this connection
    RequestBuffer *reqBuffer = getRequestBuffer(fdesc);
    if (reqBuffer == NULL) {
        reqBuffer = new RequestBuffer();
        addRequestBuffer(fdesc, reqBuffer);
    }

    // SINGLE recv() call per poll() event - strict compliance with subject
    bytesRead = recv(fdesc, buffer, BUFFER_SIZE, 0);
    if (bytesRead < 0) {
        // No data available (EAGAIN/EWOULDBLOCK) - return empty to wait for next poll
        return "";
    }
    if (bytesRead == 0) {
        // Connection closed by client - return what we have (caller will handle incomplete data)
        std::string result = reqBuffer->buffer;
        removeRequestBuffer(fdesc);
        return result;
    }

    buffer[bytesRead] = '\0';
    reqBuffer->buffer += buffer;

    // Check if we have complete headers
    std::size_t headerEndPos = reqBuffer->buffer.find("\r\n\r\n");
    if (headerEndPos == std::string::npos) {
        // Headers not complete yet, return empty to signal "not ready"
        return "";
    }

    // Check for Transfer-Encoding: chunked
    std::string transferEncoding;
    std::size_t tePos = reqBuffer->buffer.find("Transfer-Encoding:");
    if (tePos != std::string::npos && tePos < headerEndPos) {
        std::size_t teEnd = reqBuffer->buffer.find("\r\n", tePos);
        if (teEnd != std::string::npos) {
            transferEncoding = reqBuffer->buffer.substr(tePos + 18, teEnd - tePos - 18);
            // Trim whitespace
            while (!transferEncoding.empty() && transferEncoding[0] == ' ') {
                transferEncoding = transferEncoding.substr(1);
            }
        }
    }

    // Convert to lowercase for case-insensitive comparison
    std::string lowerTE = transferEncoding;
    for (std::size_t i = 0; i < lowerTE.length(); i++) {
        if (lowerTE[i] >= 'A' && lowerTE[i] <= 'Z') {
            lowerTE[i] = lowerTE[i] + 32;
        }
    }

    if (lowerTE.find("chunked") != std::string::npos) {
        // Chunked encoding - check if we have complete body (ends with 0\r\n\r\n)
        // Optimize: check only the end of buffer instead of searching entire 100MB
        std::size_t bufLen = reqBuffer->buffer.length();
        if (bufLen < headerEndPos + 5) {
            return "";  // Too short to contain terminator
        }

        // Check if buffer ends with "0\r\n\r\n" (last 5 characters)
        std::string ending = reqBuffer->buffer.substr(bufLen - 5);
        if (ending != "0\r\n\r\n") {
            return "";  // Not complete yet, wait for more data
        }

        // Complete chunked request
        std::string completeRequest = reqBuffer->buffer;
        removeRequestBuffer(fdesc);
        return completeRequest;
    }

    // Headers complete, check for Content-Length
    std::size_t totalContentLength;
    std::string fullRequest = reqBuffer->buffer;
    if (processContentLength(reqBuffer->buffer, headerEndPos, totalContentLength, fullRequest,
                             fdesc)) {
        // Request complete with body
        removeRequestBuffer(fdesc);
        return fullRequest;
    }

    // processContentLength returned false, which means one of:
    // 1. No Content-Length header (GET/DELETE/HEAD) - OK to process immediately
    // 2. Large file (>= LARGE_FILE_THRESHOLD) - handled separately by streaming
    // 3. Small file body incomplete - WAIT for more data

    // Check if there's a Content-Length header but body is incomplete
    std::string headersSection = reqBuffer->buffer.substr(0, headerEndPos);
    std::size_t contentLengthPos = headersSection.find("Content-Length:");

    if (contentLengthPos != std::string::npos) {
        // Has Content-Length - check if we have complete body
        std::size_t valueStart = contentLengthPos + CONTENT_LENGTH_HEADER;
        std::size_t lineEnd = headersSection.find("\r\n", valueStart);
        if (lineEnd != std::string::npos) {
            std::string lengthStr = headersSection.substr(valueStart, lineEnd - valueStart);
            while (!lengthStr.empty() && lengthStr[0] == ' ')
                lengthStr = lengthStr.substr(1);
            while (!lengthStr.empty() && lengthStr[lengthStr.length() - 1] == ' ')
                lengthStr = lengthStr.substr(0, lengthStr.length() - 1);

            std::size_t contentLength = stringToNumber(lengthStr);

            // If it's a large file, let streaming handle it
            if (contentLength >= LARGE_FILE_THRESHOLD) {
                std::string completeRequest = reqBuffer->buffer;
                removeRequestBuffer(fdesc);
                return completeRequest;
            }

            // For small files, check if body is complete
            std::size_t bodyStart = headerEndPos + 4;
            std::size_t currentBodySize =
                reqBuffer->buffer.length() > bodyStart ? reqBuffer->buffer.length() - bodyStart : 0;

            if (currentBodySize < contentLength) {
                // Body not complete yet, wait for more data
                return "";
            }
        }
    }

    // No Content-Length or body complete - return request
    std::string completeRequest = reqBuffer->buffer;
    removeRequestBuffer(fdesc);
    return completeRequest;
}

bool Monitor::processContentLength(const std::string &rawRequest, std::size_t headerEndPos,
                                   std::size_t &totalContentLength, std::string &fullRequest,
                                   int fdesc) {
    Logger      logger(std::cout, true);
    std::string headersSection = rawRequest.substr(0, headerEndPos);
    std::size_t contentLengthPos = headersSection.find("Content-Length:");

    if (contentLengthPos == std::string::npos) {
        return false;
    }

    std::size_t valueStart = contentLengthPos + CONTENT_LENGTH_HEADER;
    std::size_t lineEnd = headersSection.find("\r\n", valueStart);

    if (lineEnd == std::string::npos) {
        return false;
    }

    std::string lengthStr = headersSection.substr(valueStart, lineEnd - valueStart);

    while (!lengthStr.empty() && lengthStr[0] == ' ') {
        lengthStr = lengthStr.substr(1);
    }
    while (!lengthStr.empty() && lengthStr[lengthStr.length() - 1] == ' ') {
        lengthStr = lengthStr.substr(0, lengthStr.length() - 1);
    }

    totalContentLength = stringToNumber(lengthStr);

    // EARLY VALIDATION: Check client_max_body_size BEFORE accumulating more data
    // This prevents memory exhaustion attacks with small files that exceed limits
    // Extract request path from first line
    std::size_t firstLineEnd = rawRequest.find("\r\n");
    if (firstLineEnd != std::string::npos) {
        std::string firstLine = rawRequest.substr(0, firstLineEnd);
        // Parse "METHOD /path HTTP/1.x"
        std::size_t pathStart = firstLine.find(' ');
        if (pathStart != std::string::npos) {
            pathStart++;
            std::size_t pathEnd = firstLine.find(' ', pathStart);
            if (pathEnd != std::string::npos) {
                std::string requestPath = firstLine.substr(pathStart, pathEnd - pathStart);
                // Remove query string if present
                std::size_t queryPos = requestPath.find('?');
                if (queryPos != std::string::npos) {
                    requestPath = requestPath.substr(0, queryPos);
                }

                // Find server by connection port
                int                   serverPort = this->getPortForConnection(fdesc);
                const Config::Server *matchingServer = NULL;
                for (std::size_t i = 0; i < this->servers.size(); ++i) {
                    for (std::size_t j = 0; j < this->servers[i].listens.size(); ++j) {
                        if (ntohs(this->servers[i].listens[j].second) == serverPort) {
                            matchingServer = &this->servers[i];
                            break;
                        }
                    }
                    if (matchingServer)
                        break;
                }

                // Find matching location
                if (matchingServer != NULL) {
                    const Config::Location *matchingLocation = NULL;
                    std::size_t             bestMatchLength = 0;

                    for (std::size_t i = 0; i < matchingServer->locations.size(); ++i) {
                        const Config::Location &loc = matchingServer->locations[i];
                        if (requestPath.find(loc.path) == 0) {
                            bool isValidMatch = false;
                            if (loc.path == "/") {
                                isValidMatch = true;
                            } else if (requestPath.length() == loc.path.length()) {
                                isValidMatch = true;
                            } else if (requestPath[loc.path.length()] == '/') {
                                isValidMatch = true;
                            }

                            if (isValidMatch && loc.path.length() > bestMatchLength) {
                                matchingLocation = &loc;
                                bestMatchLength = loc.path.length();
                            }
                        }
                    }

                    // Check client_max_body_size limit
                    if (matchingLocation != NULL && matchingLocation->clientMaxBodySize > 0 &&
                        totalContentLength > matchingLocation->clientMaxBodySize) {
                        logger.warn() << "Early rejection: Content-Length " << totalContentLength
                                      << " exceeds client_max_body_size "
                                      << matchingLocation->clientMaxBodySize << " for path " << requestPath;

                        // Send 413 Payload Too Large immediately
                        HttpResponse errorResponse(HTTP_PAYLOAD_TOO_LARGE, logger);
                        errorResponse.setHeader("Content-Type", "text/html");
                        errorResponse.setHeader("Connection", "close");
                        errorResponse.setBody(
                            "<html><head><title>413 Payload Too Large</title></head>"
                            "<body><h1>413 Payload Too Large</h1>"
                            "<p>The request body exceeds the maximum allowed size.</p></body></html>");

                        std::string response = errorResponse.toString();
                        send(fdesc, response.c_str(), response.length(), 0);

                        // Set SO_LINGER to ensure the 413 response is sent before close
                        struct linger ling;
                        ling.l_onoff = 1;
                        ling.l_linger = 1;
                        setsockopt(fdesc, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling));

                        // Clean up and close connection
                        removeRequestBuffer(fdesc);
                        this->closePollFd(fdesc);

                        // Return true to signal "handled" (caller won't process further)
                        fullRequest = "";
                        return true;
                    }
                }
            }
        }
    }

    // Don't try to read large files in memory - let streaming handle them
    if (totalContentLength >= LARGE_FILE_THRESHOLD) {
        return false;
    }

    std::size_t bodyStart = headerEndPos + 4;
    std::size_t currentBodySize = rawRequest.length() > bodyStart ? rawRequest.length() - bodyStart : 0;

    // Check if we have the complete body in the current buffer
    if (currentBodySize < totalContentLength) {
        // Body not complete yet - return false so readHttpRequest() waits for more data
        return false;
    }

    // Body is complete, return it
    fullRequest = rawRequest;
    return true;
}

Monitor::ExecResult Monitor::processHttpRequest(int fdesc, const std::string &rawRequest,
                                                int &ready) {
    // Check for large upload first
    std::size_t headerEndPos = rawRequest.find("\r\n\r\n");
    if (headerEndPos != std::string::npos) {
        std::size_t contentLengthPos = rawRequest.find("Content-Length:");
        if (contentLengthPos != std::string::npos && contentLengthPos < headerEndPos) {
            std::size_t contentLength = extractContentLength(rawRequest, contentLengthPos);
            if (UploadManager::isLargeFile(contentLength)) {
                logger.info() << "Large upload detected (" << contentLength
                              << " bytes), using streaming to disk";

                // Validate against client_max_body_size BEFORE starting streaming
                HttpRequest tempRequest;
                tempRequest.parse(rawRequest);
                std::string requestPath = tempRequest.getPath();

                // Find server by port
                int serverPort = this->getPortForConnection(fdesc);
                const Config::Server* matchingServer = NULL;
                for (std::size_t i = 0; i < this->servers.size(); ++i) {
                    for (std::size_t j = 0; j < this->servers[i].listens.size(); ++j) {
                        if (ntohs(this->servers[i].listens[j].second) == serverPort) {
                            matchingServer = &this->servers[i];
                            break;
                        }
                    }
                    if (matchingServer) break;
                }

                // Find matching location
                if (matchingServer != NULL) {
                    const Config::Location* matchingLocation = NULL;
                    std::size_t bestMatchLength = 0;

                    for (std::size_t i = 0; i < matchingServer->locations.size(); ++i) {
                        const Config::Location& loc = matchingServer->locations[i];
                        if (requestPath.find(loc.path) == 0) {
                            // Verify valid directory match
                            bool isValidMatch = false;
                            if (loc.path == "/") {
                                isValidMatch = true;
                            } else if (requestPath.length() == loc.path.length()) {
                                isValidMatch = true;
                            } else if (requestPath[loc.path.length()] == '/') {
                                isValidMatch = true;
                            }

                            if (isValidMatch && loc.path.length() > bestMatchLength) {
                                matchingLocation = &loc;
                                bestMatchLength = loc.path.length();
                            }
                        }
                    }

                    // Check client_max_body_size limit
                    if (matchingLocation != NULL && matchingLocation->clientMaxBodySize > 0 &&
                        contentLength > matchingLocation->clientMaxBodySize) {
                        logger.warn() << "Large upload rejected: " << contentLength << " > "
                                     << matchingLocation->clientMaxBodySize << " (client_max_body_size)";

                        // Send 413 Payload Too Large
                        HttpResponse errorResponse(HTTP_PAYLOAD_TOO_LARGE, logger);
                        errorResponse.setHeader("Content-Type", "text/html");
                        errorResponse.setBody("<h1>413 Payload Too Large</h1>"
                                            "<p>Upload size exceeds client_max_body_size limit</p>");
                        sendHttpResponse(fdesc, errorResponse);
                        ready--;
                        return Monitor::EXEC_SUCCESS;
                    }
                }

                // Validation passed, proceed with streaming
                Monitor::HeaderPosition headerPos(headerEndPos);
                Monitor::ContentLength  contentLen(contentLength);
                UploadInfo              uploadInfo(headerPos, contentLen);
                return handleLargeUpload(fdesc, rawRequest, uploadInfo, ready);
            }
        }
    }

    // Process regular request
    HttpRequest httpRequest;
    logger.info() << "About to parse raw request (length: " << rawRequest.length() << " bytes)";
    if (rawRequest.length() > 0) {
        logger.info() << "Raw request first 200 chars: '"
                     << rawRequest.substr(0, std::min(rawRequest.length(), size_t(200))) << "'";
    } else {
        logger.warn() << "Raw request is EMPTY!";
    }
    httpRequest.parse(rawRequest);

    HttpResponse httpResponse = generateHttpResponse(httpRequest, fdesc);
    sendHttpResponse(fdesc, httpResponse);

    ready--;

    // Only close connection if response was fully sent
    // If there's a pending response, keep connection open for POLLOUT handling
    if (getPendingResponse(fdesc) == NULL) {
        this->closePollFd(fdesc);
    } else {
        logger.info() << "Response pending on fd " << fdesc << ", keeping connection open for POLLOUT";
    }
    return Monitor::EXEC_SUCCESS;
}

std::size_t Monitor::extractContentLength(const std::string &rawRequest,
                                          std::size_t        contentLengthPos) {
    std::size_t valueStart = contentLengthPos + CONTENT_LENGTH_HEADER;
    std::size_t lineEnd = rawRequest.find("\r\n", valueStart);

    if (lineEnd == std::string::npos) {
        return 0;
    }

    std::string lengthStr = rawRequest.substr(valueStart, lineEnd - valueStart);

    while (!lengthStr.empty() && lengthStr[0] == ' ') {
        lengthStr = lengthStr.substr(1);
    }
    while (!lengthStr.empty() && lengthStr[lengthStr.length() - 1] == ' ') {
        lengthStr = lengthStr.substr(0, lengthStr.length() - 1);
    }

    return stringToNumber(lengthStr);
}

HttpResponse Monitor::generateHttpResponse(const HttpRequest &httpRequest, int fdesc) {
    if (httpRequest.isValid()) {
        int serverPort = this->getPortForConnection(fdesc);
        if (serverPort < 0) {
            if (!this->servers.empty() && !this->servers[0].listens.empty()) {
                serverPort = this->servers[0].listens[0].second;
            } else {
                serverPort = DEFAULT_SERVER_PORT;
            }
        }

        return this->httpServer->processRequest(httpRequest, serverPort);
    }

    logger.warn() << "Invalid HTTP request received (generateHttpResponse)";
    logger.warn() << "Request marked as invalid by HttpRequest parser";
    logger.warn() << "Method: '" << httpRequest.getMethod() << "' Path: '" << httpRequest.getPath()
                  << "' Version: '" << httpRequest.getVersion() << "'";

    // Return appropriate error response based on error code
    int errorCode = httpRequest.getErrorCode();
    if (errorCode == HTTP_URI_TOO_LONG) {
        return HttpResponse(HTTP_URI_TOO_LONG, "URI Too Long");
    } else if (errorCode == HTTP_HEADER_FIELDS_TOO_LARGE) {
        return HttpResponse(HTTP_HEADER_FIELDS_TOO_LARGE, "Request Header Fields Too Large");
    }

    return HttpResponse::createBadRequest();
}

void Monitor::sendHttpResponse(int fdesc, const HttpResponse &httpResponse) {
    std::string responseString = httpResponse.toString();
    size_t      totalSize = responseString.size();
    size_t      totalSent = 0;

    // Keep sending until we get EAGAIN (recommended pattern for non-blocking sockets)
    while (totalSent < totalSize) {
        size_t remaining = totalSize - totalSent;
        ssize_t sent = send(fdesc, responseString.c_str() + totalSent, remaining, 0);

        if (sent == -1) {
            // Socket buffer full or error - queue remaining data and wait for POLLOUT
            // Note: errno check forbidden by subject after I/O operations
            logger.info() << "Send returned -1 after " << totalSent << "/" << totalSize
                          << " bytes, queueing remaining data";

            // Save the remaining data
            PendingResponse *pending = new PendingResponse(responseString);
            pending->bytesSent = totalSent;
            addPendingResponse(fdesc, pending);

            // Add POLLOUT to events to be notified when socket is writable
            for (int i = 0; i < fdCount; i++) {
                if (fds[i].fd == fdesc) {
                    fds[i].events = POLLIN | POLLOUT;
                    break;
                }
            }
            return;
        }

        totalSent += sent;
    }

    // Successfully sent complete response
    logger.info() << "Successfully sent complete response (" << totalSent << " bytes)";
}

UploadState *Monitor::getUploadState(int fdesc) {
    std::map<int, UploadState *>::iterator it = activeUploads.find(fdesc);
    if (it != activeUploads.end()) {
        return it->second;
    }
    return NULL;
}

void Monitor::addUploadState(int fdesc, UploadState *state) { activeUploads[fdesc] = state; }

void Monitor::removeUploadState(int fdesc) {
    std::map<int, UploadState *>::iterator it = activeUploads.find(fdesc);
    if (it != activeUploads.end()) {
        delete it->second;
        activeUploads.erase(it);
    }
}

RequestBuffer *Monitor::getRequestBuffer(int fdesc) {
    std::map<int, RequestBuffer *>::iterator it = requestBuffers.find(fdesc);
    if (it != requestBuffers.end()) {
        return it->second;
    }
    return NULL;
}

void Monitor::addRequestBuffer(int fdesc, RequestBuffer *buffer) {
    requestBuffers[fdesc] = buffer;
}

void Monitor::removeRequestBuffer(int fdesc) {
    std::map<int, RequestBuffer *>::iterator it = requestBuffers.find(fdesc);
    if (it != requestBuffers.end()) {
        delete it->second;
        requestBuffers.erase(it);
    }
}

PendingResponse *Monitor::getPendingResponse(int fdesc) {
    std::map<int, PendingResponse *>::iterator it = pendingResponses.find(fdesc);
    if (it != pendingResponses.end()) {
        return it->second;
    }
    return NULL;
}

void Monitor::addPendingResponse(int fdesc, PendingResponse *response) {
    pendingResponses[fdesc] = response;
}

void Monitor::removePendingResponse(int fdesc) {
    std::map<int, PendingResponse *>::iterator it = pendingResponses.find(fdesc);
    if (it != pendingResponses.end()) {
        // Clean up file descriptor and temp file if using file-based response
        if (it->second->tempFileFd >= 0) {
            close(it->second->tempFileFd);
            if (!it->second->tempFilePath.empty()) {
                std::remove(it->second->tempFilePath.c_str());
                logger.info() << "Cleaned up response temp file: " << it->second->tempFilePath;
            }
        }
        delete it->second;
        pendingResponses.erase(it);
    }
}

Monitor::ExecResult Monitor::continueSend(int fdesc) {
    PendingResponse *pending = getPendingResponse(fdesc);
    if (pending == NULL) {
        logger.warn() << "continueSend called but no pending response for fd " << fdesc;
        return Monitor::EXEC_SUCCESS;
    }

    // Keep sending until we get EAGAIN or complete
    while (pending->bytesSent < pending->totalSize) {
        size_t remaining = pending->totalSize - pending->bytesSent;
        ssize_t sent = send(fdesc, pending->stringData.c_str() + pending->bytesSent, remaining, 0);

        if (sent == -1) {
            // Socket buffer full or error - wait for next POLLOUT
            // Note: errno check forbidden by subject after I/O operations
            logger.info() << "Send returned -1 at " << pending->bytesSent << "/" << pending->totalSize
                          << " bytes, waiting for next POLLOUT";
            return Monitor::EXEC_SUCCESS;
        }

        pending->bytesSent += sent;
    }

    // Complete send!
    logger.info() << "Successfully sent complete response (" << pending->bytesSent << " bytes)";
    removePendingResponse(fdesc);

    // Close connection after sending response (HTTP/1.0 behavior)
    closePollFd(fdesc);

    return Monitor::EXEC_SUCCESS;
}

Monitor::ExecResult Monitor::continueUpload(int fdesc, int &ready) {
    UploadState *uploadState = getUploadState(fdesc);
    if (uploadState == NULL) {
        return Monitor::EXEC_SUCCESS;
    }

    Logger logger(std::cout, true);
    char   buffer[UPLOAD_BUFFER_SIZE];

    ssize_t bytesRead = recv(fdesc, buffer, UPLOAD_BUFFER_SIZE, 0);
    if (bytesRead <= 0) {
        if (bytesRead == 0) {
            logger.warn() << "Connection closed during large upload (received "
                          << uploadState->totalReceived << "/" << uploadState->totalContentLength
                          << " bytes)";
            uploadState->manager->cleanup();
            removeUploadState(fdesc);
            this->closePollFd(fdesc);
            return Monitor::EXEC_SUCCESS;
        }
        return Monitor::EXEC_SUCCESS;
    }

    std::size_t bytesToWrite = static_cast<std::size_t>(bytesRead);
    if (uploadState->totalReceived + bytesToWrite > uploadState->totalContentLength) {
        bytesToWrite = uploadState->totalContentLength - uploadState->totalReceived;
    }

    if (!uploadState->manager->writeChunk(buffer, bytesToWrite)) {
        logger.error() << "Failed to write chunk to disk during large upload";
        uploadState->manager->cleanup();
        removeUploadState(fdesc);
        this->closePollFd(fdesc);
        return Monitor::EXEC_SUCCESS;
    }

    uploadState->totalReceived += bytesToWrite;

    if (uploadState->totalReceived >= uploadState->totalContentLength || uploadState->manager->isComplete()) {
        if (!uploadState->manager->finishUpload()) {
            logger.error() << "Failed to finish large upload";
            uploadState->manager->cleanup();
            removeUploadState(fdesc);
            this->closePollFd(fdesc);
            return Monitor::EXEC_SUCCESS;
        }

        uploadState->manager->disableAutoCleanup();

        std::string headersOnly =
            uploadState->rawRequest.substr(0, uploadState->rawRequest.find("\r\n\r\n") + 4);
        logger.info() << "Large upload completed successfully, temp file: "
                      << uploadState->manager->getTempFilePath();

        HttpRequest httpRequest(logger);
        httpRequest.setTempFilePath(uploadState->manager->getTempFilePath());
        httpRequest.setOriginalFilename(uploadState->manager->getOriginalFilename());
        httpRequest.parse(headersOnly);

        HttpResponse httpResponse;
        if (httpRequest.isValid()) {
            int serverPort = this->getPortForConnection(fdesc);
            if (serverPort < 0) {
                if (!this->servers.empty() && !this->servers[0].listens.empty()) {
                    serverPort = this->servers[0].listens[0].second;
                } else {
                    serverPort = DEFAULT_SERVER_PORT;
                }
            }
            httpResponse = this->httpServer->processRequest(httpRequest, serverPort);
        } else {
            logger.warn() << "Invalid HTTP request received (active upload continuation path)";
            logger.warn() << "Method: '" << httpRequest.getMethod() << "' Path: '" << httpRequest.getPath()
                         << "' Version: '" << httpRequest.getVersion() << "'";
            logger.warn() << "Headers only (first 200 chars): "
                         << headersOnly.substr(0, std::min(headersOnly.length(), size_t(200)));
            httpResponse = HttpResponse::createBadRequest();
        }

        sendHttpResponse(fdesc, httpResponse);

        ready--;
        uploadState->manager->cleanup();
        removeUploadState(fdesc);

        // Only close connection if response was fully sent
        if (getPendingResponse(fdesc) == NULL) {
            this->closePollFd(fdesc);
        } else {
            logger.info() << "Response pending on fd " << fdesc << ", keeping connection open for POLLOUT";
        }
    }

    return Monitor::EXEC_SUCCESS;
}
