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
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>

#include <cstddef>
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
    // Check if this file descriptor has an ongoing upload
    UploadState *uploadState = getUploadState(fdesc);
    if (uploadState != NULL) {
        return continueUpload(fdesc, ready);
    }

    std::string rawRequest = readHttpRequest(fdesc);

    // If request is incomplete (empty string returned), wait for more data
    if (rawRequest.empty()) {
        ready--;
        return Monitor::EXEC_SUCCESS;
    }

    return processHttpRequest(fdesc, rawRequest, ready);
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

        std::string responseString = httpResponse.toString();
        send(fdesc, responseString.c_str(), responseString.size(), 0);

        removeUploadState(fdesc);
        this->closePollFd(fdesc);
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
    if (bytesRead <= 0) {
        // Connection closed or error
        return reqBuffer->buffer;
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

    // Headers complete, no Content-Length (GET, DELETE, HEAD, etc.)
    std::string completeRequest = reqBuffer->buffer;
    removeRequestBuffer(fdesc);
    return completeRequest;
}

bool Monitor::processContentLength(const std::string &rawRequest, std::size_t headerEndPos,
                                   std::size_t &totalContentLength, std::string &fullRequest,
                                   int fdesc) {
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

    // Don't try to read large files in memory - let streaming handle them
    if (totalContentLength >= LARGE_FILE_THRESHOLD) {
        return false;
    }

    std::size_t bodyStart = headerEndPos + 4;
    std::size_t currentBodySize = rawRequest.length() - bodyStart;

    if (currentBodySize < totalContentLength) {
        char        buffer[BUFFER_SIZE + 1];
        std::string completeRequest = rawRequest;

        while (currentBodySize < totalContentLength) {
            ssize_t moreBytesRead = recv(fdesc, buffer, BUFFER_SIZE, 0);
            if (moreBytesRead <= 0) {
                if (moreBytesRead == 0) {
                    logger.warn() << "Connection closed while reading body";
                } else {
                    logger.warn() << "Error reading body data (subject forbids errno checking)";
                }
                break;
            }
            buffer[moreBytesRead] = '\0';
            completeRequest += buffer;
            currentBodySize = completeRequest.length() - bodyStart;
        }
        fullRequest = completeRequest;
    }

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
    this->closePollFd(fdesc);
    return Monitor::EXEC_SUCCESS;
}

Monitor::ExecResult Monitor::streamRemainingData(int fdesc, UploadManager &uploadManager,
                                                 std::size_t &totalReceived,
                                                 std::size_t  totalContentLength) {
    char      buffer[UPLOAD_BUFFER_SIZE];
    Logger    logger(std::cout, true);
    int       consecutiveFailures = 0;
    const int maxConsecutiveFailures = 50000;  // Allow more retries for large uploads

    while (totalReceived < totalContentLength && consecutiveFailures < maxConsecutiveFailures) {
        ssize_t bytesRead = recv(fdesc, buffer, UPLOAD_BUFFER_SIZE, 0);
        if (bytesRead <= 0) {
            if (bytesRead == 0) {
                logger.warn() << "Connection closed during large upload (received " << totalReceived
                              << "/" << totalContentLength << " bytes)";
                uploadManager.cleanup();
                this->closePollFd(fdesc);
                return Monitor::EXEC_SUCCESS;
            }
            // recv() returned -1, which could be EAGAIN/EWOULDBLOCK or real error
            // Subject forbids checking errno after I/O operations
            // For non-blocking sockets, this typically means no more data available now
            // Increment consecutive failure count to prevent infinite loops
            consecutiveFailures++;
            continue;
        }

        // Reset consecutive failure count on successful read
        consecutiveFailures = 0;

        std::size_t bytesToWrite = static_cast<std::size_t>(bytesRead);
        if (totalReceived + bytesToWrite > totalContentLength) {
            bytesToWrite = totalContentLength - totalReceived;
        }

        if (!uploadManager.writeChunk(buffer, bytesToWrite)) {
            logger.error() << "Failed to write chunk to disk during large upload";
            uploadManager.cleanup();
            this->closePollFd(fdesc);
            return Monitor::EXEC_SUCCESS;
        }

        totalReceived += bytesToWrite;
    }

    // Check if upload completed successfully
    if (totalReceived >= totalContentLength) {
        return Monitor::EXEC_SUCCESS;
    }

    // Upload incomplete due to timeout or connection issues
    logger.warn() << "Large upload incomplete: received " << totalReceived << "/"
                  << totalContentLength << " bytes (consecutive failure limit reached)";
    uploadManager.cleanup();
    this->closePollFd(fdesc);
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
    return HttpResponse::createBadRequest();
}

void Monitor::sendHttpResponse(int fdesc, const HttpResponse &httpResponse) {
    std::string responseString = httpResponse.toString();
    send(fdesc, responseString.c_str(), responseString.size(), 0);
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

        std::string responseString = httpResponse.toString();
        send(fdesc, responseString.c_str(), responseString.size(), 0);

        ready--;
        uploadState->manager->cleanup();
        removeUploadState(fdesc);
        this->closePollFd(fdesc);
    }

    return Monitor::EXEC_SUCCESS;
}
