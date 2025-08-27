/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HttpResponse.hpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mancorte <mancorte@student.42malaga.com>   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/01/27 00:00:00 by mancorte          #+#    #+# Malaga      */
/*   Updated: 2025/01/27 00:00:00 by mancorte         ###   ########.com      */
/*                                                                            */
/* ************************************************************************** */

#ifndef HTTPRESPONSE_HPP
#define HTTPRESPONSE_HPP

/* @------------------------------------------------------------------------@ */
/* |                            Include Section                             | */
/* @------------------------------------------------------------------------@ */

#include <cstddef>  // For std::size_t

#include <map>     // For std::map
#include <string>  // For std::string

#include "Logger.hpp"

/* @------------------------------------------------------------------------@ */
/* |                             Class Section                              | */
/* @------------------------------------------------------------------------@ */

class HttpResponse {
public:
    HttpResponse();
    HttpResponse(int statusCode);
    HttpResponse(int statusCode, const std::string& statusMessage);
    HttpResponse(const Logger& logger);
    HttpResponse(int statusCode, const Logger& logger);
    ~HttpResponse();
    HttpResponse(const HttpResponse& that);
    HttpResponse& operator=(const HttpResponse& that);

    void setStatus(int code);
    void setStatus(int code, const std::string& message);
    void setHeader(const std::string& key, const std::string& value);
    void setBody(const std::string& body);
    void setBodyFromFile(const std::string& filePath);
    void appendBody(const std::string& content);

    int                getStatusCode() const;
    const std::string& getStatusMessage() const;
    const std::string& getHeader(const std::string& key) const;
    const std::string& getBody() const;
    std::size_t        getContentLength() const;

    std::string toString() const;
    void        clear();

    // Static helper methods for common responses
    static HttpResponse createOK(const std::string& body = "");
    static HttpResponse createNotFound(const std::string& message = "");
    static HttpResponse createInternalError(const std::string& message = "");
    static HttpResponse createBadRequest(const std::string& message = "");
    static HttpResponse createMethodNotAllowed(const std::string& message = "");

private:
    Logger                             m_Logger;
    int                                m_StatusCode;
    std::string                        m_StatusMessage;
    std::map<std::string, std::string> m_Headers;
    std::string                        m_Body;

    std::string getDefaultStatusMessage(int statusCode) const;
    std::string getCurrentDateTime() const;
    std::string toLowerCase(const std::string& str) const;
    void        setDefaultHeaders();
    std::string getContentType(const std::string& filePath) const;
};

/* @------------------------------------------------------------------------@ */
/* |                            Function Section                            | */
/* @------------------------------------------------------------------------@ */

#endif
