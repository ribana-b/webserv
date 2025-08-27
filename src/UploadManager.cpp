/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   UploadManager.cpp                                 :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mancorte <mancorte@student.42malaga.com>   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/01/27 12:10:00 by mancorte          #+#    #+# Malaga      */
/*   Updated: 2025/01/27 12:10:00 by mancorte         ###   ########.com      */
/*                                                                            */
/* ************************************************************************** */

#include "UploadManager.hpp"

#include <unistd.h>    // For write, close, unlink
#include <fcntl.h>     // For open, O_CREAT, etc
#include <sys/stat.h>  // For file permissions

#include <cstdlib>   // For mkstemp
#include <cstring>   // For strlen
#include <fstream>   // For std::ifstream
#include <iostream>  // For std::cout
#include <sstream>   // For std::ostringstream

#include "Logger.hpp"

/* @------------------------------------------------------------------------@ */
/* |                        Constructor/Destructor                          | */
/* @------------------------------------------------------------------------@ */

UploadManager::UploadManager() : m_Logger(std::cout, true), m_TempFd(-1), 
    m_ExpectedSize(0), m_BytesWritten(0), m_IsActive(false), m_IsComplete(false), m_AutoCleanup(true) {}

UploadManager::UploadManager(const Logger& logger) : m_Logger(logger), m_TempFd(-1),
    m_ExpectedSize(0), m_BytesWritten(0), m_IsActive(false), m_IsComplete(false), m_AutoCleanup(true) {}

UploadManager::~UploadManager() {
    if (m_AutoCleanup) {
        cleanup();
    }
}

UploadManager::UploadManager(const UploadManager& that) : m_Logger(that.m_Logger),
    m_TempFilePath(that.m_TempFilePath), m_TempFd(-1), 
    m_ExpectedSize(that.m_ExpectedSize), m_BytesWritten(that.m_BytesWritten),
    m_IsActive(false), m_IsComplete(that.m_IsComplete), m_AutoCleanup(that.m_AutoCleanup) {
    // Note: Don't copy file descriptor, each instance should manage its own
}

UploadManager& UploadManager::operator=(const UploadManager& that) {
    if (this != &that) {
        cleanup(); // Clean up current state
        
        m_Logger = that.m_Logger;
        m_TempFilePath = that.m_TempFilePath;
        m_TempFd = -1;
        m_ExpectedSize = that.m_ExpectedSize;
        m_BytesWritten = that.m_BytesWritten;
        m_IsActive = false;
        m_IsComplete = that.m_IsComplete;
    }
    return *this;
}

/* @------------------------------------------------------------------------@ */
/* |                          Main Streaming Methods                        | */
/* @------------------------------------------------------------------------@ */

bool UploadManager::startLargeUpload(std::size_t contentLength) {
    if (m_IsActive) {
        m_Logger.warn() << "UploadManager: Cannot start new upload, one already in progress";
        return false;
    }

    m_ExpectedSize = contentLength;
    m_BytesWritten = 0;
    m_IsComplete = false;

    if (!createTempFile()) {
        m_Logger.error() << "UploadManager: Failed to create temporary file";
        return false;
    }

    m_IsActive = true;
    m_Logger.info() << "UploadManager: Started streaming upload for " 
                    << contentLength << " bytes to " << m_TempFilePath;
    return true;
}

bool UploadManager::writeChunk(const char* data, std::size_t size) {
    if (!m_IsActive || m_TempFd == -1) {
        m_Logger.warn() << "UploadManager: Cannot write chunk, upload not active";
        return false;
    }

    if (m_BytesWritten + size > m_ExpectedSize) {
        m_Logger.warn() << "UploadManager: Chunk would exceed expected size ("
                        << (m_BytesWritten + size) << " > " << m_ExpectedSize << ")";
        return false;
    }

    ssize_t bytesWritten = write(m_TempFd, data, size);
    if (bytesWritten == -1) {
        m_Logger.error() << "UploadManager: Failed to write chunk to temp file";
        return false;
    }

    if (static_cast<std::size_t>(bytesWritten) != size) {
        m_Logger.error() << "UploadManager: Partial write (" << bytesWritten 
                         << "/" << size << " bytes)";
        return false;
    }

    m_BytesWritten += size;

    return true;
}

bool UploadManager::finishUpload() {
    if (!m_IsActive) {
        m_Logger.warn() << "UploadManager: Cannot finish upload, not active";
        return false;
    }

    if (m_BytesWritten != m_ExpectedSize) {
        m_Logger.warn() << "UploadManager: Upload incomplete (" 
                        << m_BytesWritten << "/" << m_ExpectedSize << " bytes)";
        return false;
    }

    closeTempFile();
    m_IsActive = false;
    m_IsComplete = true;
    
    m_Logger.info() << "UploadManager: Upload completed successfully (" 
                    << m_BytesWritten << " bytes) -> " << m_TempFilePath;
    return true;
}

void UploadManager::cleanup() {
    if (m_TempFd != -1) {
        closeTempFile();
    }
    
    if (!m_TempFilePath.empty()) {
        deleteTempFile();
        m_TempFilePath.clear();
    }
    
    m_IsActive = false;
    m_IsComplete = false;
    m_BytesWritten = 0;
    m_ExpectedSize = 0;
}

/* @------------------------------------------------------------------------@ */
/* |                          Status and Info Methods                       | */
/* @------------------------------------------------------------------------@ */

bool UploadManager::isLargeUpload() const {
    return m_ExpectedSize >= LARGE_FILE_THRESHOLD;
}

bool UploadManager::isComplete() const {
    return m_IsComplete;
}

const std::string& UploadManager::getTempFilePath() const {
    return m_TempFilePath;
}

std::size_t UploadManager::getBytesWritten() const {
    return m_BytesWritten;
}

std::size_t UploadManager::getExpectedSize() const {
    return m_ExpectedSize;
}

/* @------------------------------------------------------------------------@ */
/* |                            Utility Methods                             | */
/* @------------------------------------------------------------------------@ */

std::string UploadManager::readFromTempFile() const {
    if (!m_IsComplete || m_TempFilePath.empty()) {
        // Use const_cast to work around Logger's non-const methods
        const_cast<Logger&>(m_Logger).warn() << "UploadManager: Cannot read from temp file, upload not complete";
        return "";
    }

    std::ifstream file(m_TempFilePath.c_str(), std::ios::binary);
    if (!file.is_open()) {
        const_cast<Logger&>(m_Logger).error() << "UploadManager: Failed to open temp file for reading: " << m_TempFilePath;
        return "";
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    file.close();

    std::string content = buffer.str();
    return content;
}

bool UploadManager::moveTempFile(const std::string& destination) {
    if (!m_IsComplete || m_TempFilePath.empty()) {
        m_Logger.warn() << "UploadManager: Cannot move temp file, upload not complete";
        return false;
    }

    // Use system rename for atomic move operation
    if (rename(m_TempFilePath.c_str(), destination.c_str()) != 0) {
        m_Logger.error() << "UploadManager: Failed to move " << m_TempFilePath 
                         << " to " << destination;
        return false;
    }

    m_Logger.info() << "UploadManager: Moved temp file " << m_TempFilePath 
                    << " to " << destination;
    
    m_TempFilePath = destination; // Update path to new location
    return true;
}

void UploadManager::disableAutoCleanup() {
    m_AutoCleanup = false;
}

bool UploadManager::isLargeFile(std::size_t contentLength) {
    return contentLength >= LARGE_FILE_THRESHOLD;
}

/* @------------------------------------------------------------------------@ */
/* |                            Private Methods                             | */
/* @------------------------------------------------------------------------@ */

std::string UploadManager::generateTempFilePath() {
    char tempTemplate[] = "/tmp/webserv_upload_XXXXXX";
    int fd = mkstemp(tempTemplate);
    if (fd == -1) {
        return "";
    }
    
    // Close the fd immediately, we'll reopen it properly
    close(fd);
    return std::string(tempTemplate);
}

bool UploadManager::createTempFile() {
    m_TempFilePath = generateTempFilePath();
    if (m_TempFilePath.empty()) {
        m_Logger.error() << "UploadManager: Failed to generate temp file path";
        return false;
    }

    // Open with write-only, create if not exists, truncate if exists
    m_TempFd = open(m_TempFilePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (m_TempFd == -1) {
        m_Logger.error() << "UploadManager: Failed to open temp file: " << m_TempFilePath;
        m_TempFilePath.clear();
        return false;
    }

    return true;
}

void UploadManager::closeTempFile() {
    if (m_TempFd != -1) {
        close(m_TempFd);
        m_TempFd = -1;
    }
}

void UploadManager::deleteTempFile() {
    if (!m_TempFilePath.empty()) {
        if (unlink(m_TempFilePath.c_str()) == 0) {
        } else {
            m_Logger.warn() << "UploadManager: Failed to delete temp file: " << m_TempFilePath;
        }
    }
}
