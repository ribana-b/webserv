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

#include <fcntl.h>     // For open, O_CREAT, etc
#include <sys/stat.h>  // For file permissions
#include <unistd.h>    // For write, close

#include <cstdio>    // For std::remove
#include <cstring>   // For strlen
#include <fstream>   // For std::ifstream
#include <iostream>  // For std::cout
#include <sstream>   // For std::ostringstream

#include "Logger.hpp"

/* @------------------------------------------------------------------------@ */
/* |                        Constructor/Destructor                          | */
/* @------------------------------------------------------------------------@ */

UploadManager::UploadManager() :
    m_Logger(std::cout, true),
    m_TempFd(-1),
    m_ExpectedSize(0),
    m_BytesWritten(0),
    m_IsActive(false),
    m_IsComplete(false),
    m_AutoCleanup(true),
    m_ParserState(MULTIPART_DISABLED) {}

UploadManager::UploadManager(const Logger& logger) :
    m_Logger(logger),
    m_TempFd(-1),
    m_ExpectedSize(0),
    m_BytesWritten(0),
    m_IsActive(false),
    m_IsComplete(false),
    m_AutoCleanup(true),
    m_ParserState(MULTIPART_DISABLED) {}

UploadManager::~UploadManager() {
    if (m_AutoCleanup) {
        cleanup();
    }
}

UploadManager::UploadManager(const UploadManager& that) :
    m_Logger(that.m_Logger),
    m_TempFilePath(that.m_TempFilePath),
    m_TempFd(-1),
    m_ExpectedSize(that.m_ExpectedSize),
    m_BytesWritten(that.m_BytesWritten),
    m_IsActive(false),
    m_IsComplete(that.m_IsComplete),
    m_AutoCleanup(that.m_AutoCleanup) {
    // Note: Don't copy file descriptor, each instance should manage its own
}

UploadManager& UploadManager::operator=(const UploadManager& that) {
    if (this != &that) {
        cleanup();  // Clean up current state

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
    return startLargeUpload(contentLength, "");
}

bool UploadManager::startLargeUpload(std::size_t contentLength, const std::string& boundary) {
    if (m_IsActive) {
        m_Logger.warn() << "UploadManager: Cannot start new upload, one already in progress";
        return false;
    }

    m_ExpectedSize = contentLength;
    m_BytesWritten = 0;
    m_IsComplete = false;
    m_ParserBuffer.clear();

    if (!boundary.empty()) {
        m_Boundary = "\r\n--" + boundary;
        m_ParserState = SEARCHING_HEADERS;
        m_Logger.info() << "UploadManager: Multipart mode enabled with boundary: " << boundary;
    } else {
        m_Boundary.clear();
        m_ParserState = MULTIPART_DISABLED;
    }

    if (!createTempFile()) {
        m_Logger.error() << "UploadManager: Failed to create temporary file";
        return false;
    }

    m_IsActive = true;
    m_Logger.info() << "UploadManager: Started streaming upload for " << contentLength
                    << " bytes to " << m_TempFilePath;
    return true;
}

bool UploadManager::writeChunk(const char* data, std::size_t size) {
    if (!m_IsActive || m_TempFd == -1) {
        m_Logger.warn() << "UploadManager: Cannot write chunk, upload not active";
        return false;
    }

    // If multipart parsing is disabled, write directly
    if (m_ParserState == MULTIPART_DISABLED) {
        ssize_t bytesWritten = write(m_TempFd, data, size);
        if (bytesWritten == -1 || static_cast<std::size_t>(bytesWritten) != size) {
            m_Logger.error() << "UploadManager: Failed to write chunk to temp file";
            return false;
        }
        m_BytesWritten += size;
        return true;
    }

    // Multipart parsing mode
    for (std::size_t i = 0; i < size; ++i) {
        m_ParserBuffer += data[i];

        if (m_ParserState == SEARCHING_HEADERS) {
            // Look for end of headers ("\r\n\r\n")
            if (m_ParserBuffer.length() >= 4) {
                std::size_t headerEnd = m_ParserBuffer.find("\r\n\r\n");
                if (headerEnd != std::string::npos) {
                    // Extract filename from multipart headers before clearing
                    std::size_t filenamePos = m_ParserBuffer.find("filename=\"");
                    if (filenamePos != std::string::npos && filenamePos < headerEnd) {
                        std::size_t start = filenamePos + 10;
                        std::size_t end = m_ParserBuffer.find("\"", start);
                        if (end != std::string::npos && end <= headerEnd) {
                            std::string filename = m_ParserBuffer.substr(start, end - start);
                            // Security: keep only basename
                            std::size_t lastSlash = filename.rfind('/');
                            if (lastSlash != std::string::npos) filename = filename.substr(lastSlash + 1);
                            lastSlash = filename.rfind('\\');
                            if (lastSlash != std::string::npos) filename = filename.substr(lastSlash + 1);
                            // Security: sanitize characters
                            std::string safe;
                            for (std::size_t j = 0; j < filename.length(); ++j) {
                                char c = filename[j];
                                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                    (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-') {
                                    safe += c;
                                }
                            }
                            m_OriginalFilename = safe;
                            m_Logger.info() << "UploadManager: Extracted filename: " << m_OriginalFilename;
                        }
                    }
                    m_ParserState = READING_FILE_DATA;
                    m_ParserBuffer.clear();
                    m_Logger.info() << "UploadManager: Found end of multipart headers, starting file data";
                }
            }
        } else if (m_ParserState == READING_FILE_DATA) {
            // Keep buffer size manageable (boundary length + safety margin)
            std::size_t maxBufferSize = m_Boundary.length() + 10;

            if (m_ParserBuffer.length() > maxBufferSize) {
                // Write oldest byte to file
                char byte = m_ParserBuffer[0];
                if (write(m_TempFd, &byte, 1) != 1) {
                    m_Logger.error() << "UploadManager: Failed to write byte to temp file";
                    return false;
                }
                m_BytesWritten++;
                m_ParserBuffer.erase(0, 1);
            }

            // Check if buffer contains boundary
            std::size_t boundaryPos = m_ParserBuffer.find(m_Boundary);
            if (boundaryPos != std::string::npos) {
                // Write any file data that comes BEFORE the boundary
                // m_Boundary starts with \r\n, so boundaryPos points to the \r\n before --boundary
                if (boundaryPos > 0) {
                    if (write(m_TempFd, m_ParserBuffer.c_str(), boundaryPos) !=
                        static_cast<ssize_t>(boundaryPos)) {
                        m_Logger.error() << "UploadManager: Failed to write final bytes to temp file";
                        return false;
                    }
                    m_BytesWritten += boundaryPos;
                }
                m_ParserState = DETECTED_BOUNDARY;
                m_IsComplete = true;
                closeTempFile();
                m_Logger.info() << "UploadManager: Detected multipart boundary, upload complete ("
                               << m_BytesWritten << " bytes of actual file data)";
                return true;
            }
        }
    }

    return true;
}

bool UploadManager::finishUpload() {
    if (!m_IsActive) {
        m_Logger.warn() << "UploadManager: Cannot finish upload, not active";
        return false;
    }

    // For multipart uploads, check if boundary was detected
    if (m_ParserState != MULTIPART_DISABLED) {
        if (m_ParserState != DETECTED_BOUNDARY && !m_IsComplete) {
            m_Logger.warn() << "UploadManager: Multipart upload incomplete, boundary not detected";
            return false;
        }
        // Multipart completed successfully when boundary detected
    } else {
        // For non-multipart uploads, check exact byte count
        if (m_BytesWritten != m_ExpectedSize) {
            m_Logger.warn() << "UploadManager: Upload incomplete (" << m_BytesWritten << "/"
                            << m_ExpectedSize << " bytes)";
            return false;
        }
    }

    closeTempFile();
    m_IsActive = false;
    m_IsComplete = true;

    m_Logger.info() << "UploadManager: Upload completed successfully (" << m_BytesWritten
                    << " bytes) -> " << m_TempFilePath;
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

bool UploadManager::isLargeUpload() const { return m_ExpectedSize >= LARGE_FILE_THRESHOLD; }

bool UploadManager::isComplete() const { return m_IsComplete; }

const std::string& UploadManager::getTempFilePath() const { return m_TempFilePath; }

std::size_t UploadManager::getBytesWritten() const { return m_BytesWritten; }

std::size_t UploadManager::getExpectedSize() const { return m_ExpectedSize; }

const std::string& UploadManager::getOriginalFilename() const { return m_OriginalFilename; }

/* @------------------------------------------------------------------------@ */
/* |                            Utility Methods                             | */
/* @------------------------------------------------------------------------@ */

std::string UploadManager::readFromTempFile() const {
    if (!m_IsComplete || m_TempFilePath.empty()) {
        // Use const_cast to work around Logger's non-const methods
        const_cast<Logger&>(m_Logger).warn()
            << "UploadManager: Cannot read from temp file, upload not complete";
        return "";
    }

    std::ifstream file(m_TempFilePath.c_str(), std::ios::binary);
    if (!file.is_open()) {
        const_cast<Logger&>(m_Logger).error()
            << "UploadManager: Failed to open temp file for reading: " << m_TempFilePath;
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

    // Copy file using read/write (rename not in allowed functions)
    int srcFd = open(m_TempFilePath.c_str(), O_RDONLY);
    if (srcFd < 0) {
        m_Logger.error() << "UploadManager: Failed to open source file: " << m_TempFilePath;
        return false;
    }

    int dstFd = open(destination.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dstFd < 0) {
        close(srcFd);
        m_Logger.error() << "UploadManager: Failed to open destination file: " << destination;
        return false;
    }

    char buffer[8192];
    ssize_t bytesRead;
    bool success = true;
    while ((bytesRead = read(srcFd, buffer, sizeof(buffer))) > 0) {
        if (write(dstFd, buffer, bytesRead) != bytesRead) {
            success = false;
            break;
        }
    }

    close(srcFd);
    close(dstFd);

    if (!success || bytesRead < 0) {
        m_Logger.error() << "UploadManager: Failed to copy " << m_TempFilePath << " to "
                         << destination;
        return false;
    }

    // Delete original temp file
    std::remove(m_TempFilePath.c_str());

    m_Logger.info() << "UploadManager: Moved temp file " << m_TempFilePath << " to " << destination;

    m_TempFilePath = destination;  // Update path to new location
    return true;
}

void UploadManager::disableAutoCleanup() { m_AutoCleanup = false; }

bool UploadManager::isLargeFile(std::size_t contentLength) {
    return contentLength >= LARGE_FILE_THRESHOLD;
}

/* @------------------------------------------------------------------------@ */
/* |                            Private Methods                             | */
/* @------------------------------------------------------------------------@ */

std::string UploadManager::generateTempFilePath() {
    // Generate unique filename without mkstemp (not in allowed functions)
    static unsigned int counter = 0;
    std::ostringstream oss;
    oss << "/tmp/.webserv_upload_" << ++counter;
    std::string tempPath = oss.str();

    // Try to create file with O_EXCL to ensure uniqueness
    int fd = open(tempPath.c_str(), O_WRONLY | O_CREAT | O_EXCL, TEMP_FILE_PERMISSIONS);
    if (fd == -1) {
        // If file exists, try with different counter values
        for (int attempt = 0; attempt < 100 && fd == -1; ++attempt) {
            oss.str("");
            oss << "/tmp/.webserv_upload_" << ++counter;
            tempPath = oss.str();
            fd = open(tempPath.c_str(), O_WRONLY | O_CREAT | O_EXCL, TEMP_FILE_PERMISSIONS);
        }
        if (fd == -1) {
            return "";
        }
    }

    // Close the fd immediately, we'll reopen it properly
    close(fd);
    return tempPath;
}

bool UploadManager::createTempFile() {
    m_TempFilePath = generateTempFilePath();
    if (m_TempFilePath.empty()) {
        m_Logger.error() << "UploadManager: Failed to generate temp file path";
        return false;
    }

    // Open with write-only, create if not exists, truncate if exists
    m_TempFd = open(m_TempFilePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, TEMP_FILE_PERMISSIONS);
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
        if (std::remove(m_TempFilePath.c_str()) == 0) {
        } else {
            m_Logger.warn() << "UploadManager: Failed to delete temp file: " << m_TempFilePath;
        }
    }
}
