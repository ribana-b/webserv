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
    std::size_t result = 0;
    for (std::size_t i = 0; i < str.length(); ++i) {
        char c = str[i];
        if (c >= '0' && c <= '9') {
            result = result * 10 + (c - '0');
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
    if (uploadState) {
        return continueUpload(fdesc, ready);
    }

    std::string rawRequest = readHttpRequest(fdesc);

    return processHttpRequest(fdesc, rawRequest, ready);
}

Monitor::ExecResult Monitor::handleLargeUpload(const int fdesc, const std::string &rawRequest,
                                               const UploadInfo &uploadInfo, int &ready) {
    Logger logger(std::cout, true);

    // Create UploadManager for streaming
    UploadManager *uploadManager = new UploadManager(logger);
    if (!uploadManager->startLargeUpload(uploadInfo.totalContentLength)) {
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

    ready--;
    return Monitor::EXEC_SUCCESS;
}

std::string Monitor::readHttpRequest(int fdesc) {
    ssize_t     bytesRead = 1;
    char        buffer[BUFFER_SIZE + 1];
    std::string rawRequest;

    while (bytesRead != 0) {
        bytesRead = recv(fdesc, buffer, BUFFER_SIZE, 0);
        if (bytesRead < 0) {
            break;
        }
        buffer[bytesRead] = '\0';
        rawRequest += buffer;

        std::size_t headerEndPos = rawRequest.find("\r\n\r\n");
        if (headerEndPos != std::string::npos) {
            std::size_t totalContentLength;
            std::string fullRequest = rawRequest;
            if (processContentLength(rawRequest, headerEndPos, totalContentLength, fullRequest,
                                     fdesc)) {
                return fullRequest;
            }
            break;
        }
    }

    return rawRequest;
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
                Monitor::HeaderPosition headerPos(headerEndPos);
                Monitor::ContentLength  contentLen(contentLength);
                UploadInfo              uploadInfo(headerPos, contentLen);
                return handleLargeUpload(fdesc, rawRequest, uploadInfo, ready);
            }
        }
    }

    // Process regular request
    HttpRequest httpRequest;
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
    const int MAX_CONSECUTIVE_FAILURES = 50000;  // Allow more retries for large uploads

    while (totalReceived < totalContentLength && consecutiveFailures < MAX_CONSECUTIVE_FAILURES) {
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

    logger.warn() << "Invalid HTTP request received";
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

Monitor::ExecResult Monitor::continueUpload(int fdesc, int &ready) {
    UploadState *uploadState = getUploadState(fdesc);
    if (!uploadState) {
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

    if (uploadState->totalReceived >= uploadState->totalContentLength) {
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
            logger.warn() << "Invalid HTTP request received";
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
