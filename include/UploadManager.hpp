/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   UploadManager.hpp                                 :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mancorte <mancorte@student.42malaga.com>   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/01/27 12:10:00 by mancorte          #+#    #+# Malaga      */
/*   Updated: 2025/01/27 12:10:00 by mancorte         ###   ########.com      */
/*                                                                            */
/* ************************************************************************** */

#ifndef UPLOADMANAGER_HPP
#define UPLOADMANAGER_HPP

/* @------------------------------------------------------------------------@ */
/* |                            Include Section                             | */
/* @------------------------------------------------------------------------@ */

#include <cstddef>  // For std::size_t
#include <string>   // For std::string

#include "Logger.hpp"

/* @------------------------------------------------------------------------@ */
/* |                            Define Section                              | */
/* @------------------------------------------------------------------------@ */

#define LARGE_FILE_THRESHOLD 1048576  // 1MB
#define UPLOAD_BUFFER_SIZE   8192     // 8KB buffer for streaming

/* @------------------------------------------------------------------------@ */
/* |                             Class Section                              | */
/* @------------------------------------------------------------------------@ */

class UploadManager {
public:
    UploadManager();
    UploadManager(const Logger& logger);
    ~UploadManager();
    UploadManager(const UploadManager& that);
    UploadManager& operator=(const UploadManager& that);

    // Main streaming methods
    bool        startLargeUpload(std::size_t contentLength);
    bool        writeChunk(const char* data, std::size_t size);
    bool        finishUpload();
    void        cleanup();

    // Status and info methods
    bool               isLargeUpload() const;
    bool               isComplete() const;
    const std::string& getTempFilePath() const;
    std::size_t        getBytesWritten() const;
    std::size_t        getExpectedSize() const;

    // Utility methods
    std::string readFromTempFile() const;
    bool        moveTempFile(const std::string& destination);
    void        disableAutoCleanup();

    // Static utility
    static bool isLargeFile(std::size_t contentLength);

private:
    Logger      m_Logger;
    std::string m_TempFilePath;
    int         m_TempFd;
    std::size_t m_ExpectedSize;
    std::size_t m_BytesWritten;
    bool        m_IsActive;
    bool        m_IsComplete;
    bool        m_AutoCleanup;

    std::string generateTempFilePath();
    bool        createTempFile();
    void        closeTempFile();
    void        deleteTempFile();
};

/* @------------------------------------------------------------------------@ */
/* |                            Function Section                            | */
/* @------------------------------------------------------------------------@ */

#endif
