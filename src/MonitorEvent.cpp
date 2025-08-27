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

#include <cerrno>
#include <cstddef>
#include <cstdlib>   // For std::atoi
#include <cstring>   // For strerror
#include <iostream>
#include <string>

#include "Monitor.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "UploadManager.hpp"

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
            return 0;
        case Monitor::EXEC_FATAL_ERROR:
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
        int listenPort = this->getPortForFd(fdesc);
        this->addPollFd(newFd, listenPort);
    }
    return Monitor::EXEC_SUCCESS;
}

Monitor::ExecResult Monitor::eventExecRequest(const int fdesc, int &ready) {
    ssize_t     bytesRead = 1;
    char        buffer[BUFFER_SIZE + 1];
    std::string rawRequest;

    // Read HTTP request from socket
    while (bytesRead != 0) {
        bytesRead = recv(fdesc, buffer, BUFFER_SIZE, 0);
        if (bytesRead < 0) {
            break;
        }
        buffer[bytesRead] = '\0';
        rawRequest += buffer;
        
        // Check if we have a complete HTTP request (headers end with \r\n\r\n)
        std::size_t headerEnd = rawRequest.find("\r\n\r\n");
        if (headerEnd != std::string::npos) {
            
            // Check for Content-Length directly in headers string 
            std::string headersSection = rawRequest.substr(0, headerEnd);
            std::size_t contentLengthPos = headersSection.find("Content-Length:");
            if (contentLengthPos != std::string::npos) {
                // Extract Content-Length value
                std::size_t valueStart = contentLengthPos + 15; // "Content-Length:" length
                std::size_t lineEnd = headersSection.find("\r\n", valueStart);
                if (lineEnd != std::string::npos) {
                    std::string lengthStr = headersSection.substr(valueStart, lineEnd - valueStart);
                    // Trim whitespace
                    while (!lengthStr.empty() && lengthStr[0] == ' ') lengthStr = lengthStr.substr(1);
                    while (!lengthStr.empty() && lengthStr[lengthStr.length()-1] == ' ') lengthStr = lengthStr.substr(0, lengthStr.length()-1);
                    
                    std::size_t contentLength = static_cast<std::size_t>(std::atoi(lengthStr.c_str()));
                    std::size_t bodyStart = headerEnd + 4;
                    std::size_t currentBodySize = rawRequest.length() - bodyStart;
                    
                    
                    // Check if this is a large file upload that needs streaming
                    if (UploadManager::isLargeFile(contentLength)) {
                        logger.info() << "Large upload detected (" << contentLength << " bytes), using streaming to disk";
                        return handleLargeUpload(fdesc, rawRequest, headerEnd, contentLength, ready);
                    }
                    
                    // Small file: continue with original logic
                    if (currentBodySize < contentLength) {
                        
                        // Continue reading in loop until we have the complete body
                        while (currentBodySize < contentLength) {
                            ssize_t moreBytesRead = recv(fdesc, buffer, BUFFER_SIZE, 0);
                            if (moreBytesRead <= 0) {
                                if (moreBytesRead == 0) {
                                    logger.warn() << "Connection closed while reading body";
                                } else {
                                    logger.warn() << "Error reading body data: " << errno;
                                }
                                break;
                            }
                            buffer[moreBytesRead] = '\0';
                            rawRequest += buffer;
                            currentBodySize = rawRequest.length() - bodyStart;
                        }
                        
                        if (currentBodySize >= contentLength) {
                        }
                    } else {
                    }
                }
            }
            break;
        }
    }
    
    // Parse HTTP request
    HttpRequest httpRequest;
    httpRequest.parse(rawRequest);
    
    // Generate HTTP response using HttpServer
    HttpResponse httpResponse;
    if (httpRequest.isValid()) {
        int serverPort = this->getPortForConnection(fdesc);
        if (serverPort < 0) {
            // Use first configured server port as fallback instead of hardcoded 8080
            if (!this->servers.empty() && !this->servers[0].listens.empty()) {
                serverPort = this->servers[0].listens[0].second;
            } else {
                serverPort = 8080; // Ultimate fallback if no config available
            }
        }
        
        httpResponse = this->httpServer->processRequest(httpRequest, serverPort);
    } else {
        logger.warn() << "Invalid HTTP request received";
        httpResponse = HttpResponse::createBadRequest();
    }
    
    // Send HTTP response
    std::string responseString = httpResponse.toString();
    send(fdesc, responseString.c_str(), responseString.size(), 0);
    
    ready--;
    this->closePollFd(fdesc);
    return Monitor::EXEC_SUCCESS;
}

Monitor::ExecResult Monitor::handleLargeUpload(const int fdesc, const std::string& rawRequest, 
                                              std::size_t headerEnd, std::size_t contentLength, int &ready) {
    Logger logger(std::cout, true);
    
    // Create UploadManager for streaming
    UploadManager uploadManager(logger);
    if (!uploadManager.startLargeUpload(contentLength)) {
        logger.error() << "Failed to start large upload streaming";
        this->closePollFd(fdesc);
        return Monitor::EXEC_SUCCESS;
    }
    
    // Extract any body data already received
    std::size_t bodyStart = headerEnd + 4;
    std::size_t alreadyReceived = 0;
    if (rawRequest.length() > bodyStart) {
        alreadyReceived = rawRequest.length() - bodyStart;
        const char* bodyData = rawRequest.data() + bodyStart;
        
        if (!uploadManager.writeChunk(bodyData, alreadyReceived)) {
            logger.error() << "Failed to write initial body chunk to disk";
            uploadManager.cleanup();
            this->closePollFd(fdesc);
            return Monitor::EXEC_SUCCESS;
        }
        
    }
    
    // Continue reading remaining body data and stream to disk
    std::size_t totalReceived = alreadyReceived;
    char buffer[UPLOAD_BUFFER_SIZE];
    
    while (totalReceived < contentLength) {
        ssize_t bytesRead = recv(fdesc, buffer, UPLOAD_BUFFER_SIZE, 0);
        if (bytesRead <= 0) {
            if (bytesRead == 0) {
                logger.warn() << "Connection closed during large upload (received " 
                             << totalReceived << "/" << contentLength << " bytes)";
                uploadManager.cleanup();
                this->closePollFd(fdesc);
                return Monitor::EXEC_SUCCESS;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket would block, wait for data to be available using poll
                
                pollfd pfd;
                pfd.fd = fdesc;
                pfd.events = POLLIN;
                pfd.revents = 0;
                
                int pollResult = poll(&pfd, 1, 5000); // 5 second timeout
                if (pollResult <= 0) {
                    if (pollResult == 0) {
                        logger.warn() << "Timeout waiting for upload data";
                    } else {
                        logger.error() << "Poll error during large upload: " << strerror(errno);
                    }
                    uploadManager.cleanup();
                    this->closePollFd(fdesc);
                    return Monitor::EXEC_SUCCESS;
                }
                continue;
            } else {
                logger.error() << "Error reading large upload data: " << strerror(errno) << " (errno=" << errno << ")";
                uploadManager.cleanup();
                this->closePollFd(fdesc);
                return Monitor::EXEC_SUCCESS;
            }
        }
        
        // Don't read more than we expect
        std::size_t bytesToWrite = static_cast<std::size_t>(bytesRead);
        if (totalReceived + bytesToWrite > contentLength) {
            bytesToWrite = contentLength - totalReceived;
        }
        
        if (!uploadManager.writeChunk(buffer, bytesToWrite)) {
            logger.error() << "Failed to write chunk to disk during large upload";
            uploadManager.cleanup();
            this->closePollFd(fdesc);
            return Monitor::EXEC_SUCCESS;
        }
        
        totalReceived += bytesToWrite;
    }
    
    // Finish the upload
    if (!uploadManager.finishUpload()) {
        logger.error() << "Failed to finish large upload";
        uploadManager.cleanup();
        this->closePollFd(fdesc);
        return Monitor::EXEC_SUCCESS;
    }
    
    // Disable auto cleanup so HttpServer can process the temp file
    uploadManager.disableAutoCleanup();
    
    // Create a modified rawRequest with headers only and temp file reference
    std::string headersOnly = rawRequest.substr(0, headerEnd + 4);
    logger.info() << "Large upload completed successfully, temp file: " << uploadManager.getTempFilePath();
    
    // Parse HTTP request (headers only, body will be read from temp file)
    HttpRequest httpRequest(logger);
    // Set temp file path before parsing so parseBody() knows to skip validation
    httpRequest.setTempFilePath(uploadManager.getTempFilePath());
    httpRequest.parse(headersOnly);
    
    // Generate HTTP response using HttpServer
    HttpResponse httpResponse;
    if (httpRequest.isValid()) {
        int serverPort = this->getPortForConnection(fdesc);
        if (serverPort < 0) {
            // Use first configured server port as fallback
            if (!this->servers.empty() && !this->servers[0].listens.empty()) {
                serverPort = this->servers[0].listens[0].second;
            } else {
                serverPort = 8080; // Ultimate fallback if no config available
            }
        }
        
        httpResponse = this->httpServer->processRequest(httpRequest, serverPort);
    } else {
        logger.warn() << "Invalid HTTP request received";
        httpResponse = HttpResponse::createBadRequest();
    }
    
    // Send HTTP response
    std::string responseString = httpResponse.toString();
    send(fdesc, responseString.c_str(), responseString.size(), 0);
    
    ready--;
    
    // Manual cleanup of temp file - HttpServer has finished processing
    uploadManager.cleanup();
    
    this->closePollFd(fdesc);
    return Monitor::EXEC_SUCCESS;
}
