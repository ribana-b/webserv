/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HttpRequest.hpp                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mancorte <mancorte@student.42malaga.com>   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/01/27 00:00:00 by mancorte          #+#    #+# Malaga      */
/*   Updated: 2025/01/27 00:00:00 by mancorte         ###   ########.com      */
/*                                                                            */
/* ************************************************************************** */

#ifndef HTTPREQUEST_HPP
#define HTTPREQUEST_HPP

/* @------------------------------------------------------------------------@ */
/* |                            Include Section                             | */
/* @------------------------------------------------------------------------@ */

#include <cstddef>  // For std::size_t
#include <map>      // For std::map
#include <string>   // For std::string

#include "Logger.hpp"

/* @------------------------------------------------------------------------@ */
/* |                             Class Section                              | */
/* @------------------------------------------------------------------------@ */

class HttpRequest {
public:
    HttpRequest();
    HttpRequest(const Logger& logger);
    ~HttpRequest();
    HttpRequest(const HttpRequest& that);
    HttpRequest& operator=(const HttpRequest& that);

    bool parse(const std::string& rawData);
    bool isComplete() const;
    bool isValid() const;
    void clear();

    const std::string& getMethod() const;
    const std::string& getPath() const;
    const std::string& getVersion() const;
    const std::string& getHeader(const std::string& key) const;
    const std::string& getBody() const;
    std::size_t        getContentLength() const;

    // Large file upload support
    bool               hasLargeUpload() const;
    const std::string& getTempFilePath() const;
    void               setTempFilePath(const std::string& tempPath);
    std::string        readBodyFromTempFile() const;

private:
    Logger                             m_Logger;
    std::string                        m_Method;
    std::string                        m_Path;
    std::string                        m_Version;
    std::map<std::string, std::string> m_Headers;
    std::string                        m_Body;
    bool                               m_IsComplete;
    bool                               m_IsValid;
    std::string                        m_TempFilePath;

    bool               parseRequestLine(const std::string& line);
    bool               parseHeaders(const std::string& headerSection);
    bool               parseBody(const std::string& rawData, std::size_t headerEnd);
    static std::string toLowerCase(const std::string& str);
    static std::string trimWhitespace(const std::string& str);
    static bool        isValidMethod(const std::string& method);
    static bool        isValidVersion(const std::string& version);
};

/* @------------------------------------------------------------------------@ */
/* |                            Function Section                            | */
/* @------------------------------------------------------------------------@ */

#endif
