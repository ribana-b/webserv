/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HttpResponse.cpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mancorte <mancorte@student.42malaga.com>   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/01/27 00:00:00 by mancorte          #+#    #+# Malaga      */
/*   Updated: 2025/01/27 00:00:00 by mancorte         ###   ########.com      */
/*                                                                            */
/* ************************************************************************** */

#include "HttpResponse.hpp"
#include "HttpServer.hpp"  // For HTTP status constants

#include <sys/stat.h>  // For stat

#include <cstdlib>   // For std::atoi
#include <fstream>   // For std::ifstream
#include <iostream>  // For std::cout
#include <sstream>   // For std::ostringstream
#include <string>    // For std::string

/* @------------------------------------------------------------------------@ */
/* |                        Constructor/Destructor                          | */
/* @------------------------------------------------------------------------@ */

HttpResponse::HttpResponse() : m_Logger(std::cout, false), m_StatusCode(HTTP_OK) {
    m_StatusMessage = getDefaultStatusMessage(HTTP_OK);
    setDefaultHeaders();
}

HttpResponse::HttpResponse(int statusCode) : m_Logger(std::cout, false), m_StatusCode(statusCode) {
    m_StatusMessage = getDefaultStatusMessage(statusCode);
    setDefaultHeaders();
}

HttpResponse::HttpResponse(int statusCode, const std::string& statusMessage) :
    m_Logger(std::cout, false), m_StatusCode(statusCode), m_StatusMessage(statusMessage) {
    setDefaultHeaders();
}

HttpResponse::HttpResponse(const Logger& logger) : m_Logger(logger), m_StatusCode(HTTP_OK) {
    m_StatusMessage = getDefaultStatusMessage(HTTP_OK);
    setDefaultHeaders();
}

HttpResponse::HttpResponse(int statusCode, const Logger& logger) :
    m_Logger(logger), m_StatusCode(statusCode) {
    m_StatusMessage = getDefaultStatusMessage(statusCode);
    setDefaultHeaders();
}

HttpResponse::~HttpResponse() {}

HttpResponse::HttpResponse(const HttpResponse& that) :
    m_Logger(that.m_Logger),
    m_StatusCode(that.m_StatusCode),
    m_StatusMessage(that.m_StatusMessage),
    m_Headers(that.m_Headers),
    m_Body(that.m_Body) {}

HttpResponse& HttpResponse::operator=(const HttpResponse& that) {
    if (this != &that) {
        m_StatusCode = that.m_StatusCode;
        m_StatusMessage = that.m_StatusMessage;
        m_Headers = that.m_Headers;
        m_Body = that.m_Body;
    }
    return (*this);
}

/* @------------------------------------------------------------------------@ */
/* |                             Public Methods                             | */
/* @------------------------------------------------------------------------@ */

void HttpResponse::setStatus(int code) {
    m_StatusCode = code;
    m_StatusMessage = getDefaultStatusMessage(code);
}

void HttpResponse::setStatus(int code, const std::string& message) {
    m_StatusCode = code;
    m_StatusMessage = message;
}

void HttpResponse::setHeader(const std::string& key, const std::string& value) {
    m_Headers[key] = value;
}

void HttpResponse::setBody(const std::string& body) {
    m_Body = body;

    // Update Content-Length automatically
    std::ostringstream oss;
    oss << body.length();
    setHeader("Content-Length", oss.str());
}

void HttpResponse::setBodyFromFile(const std::string& filePath) {
    std::ifstream file(filePath.c_str());
    if (!file.is_open()) {
        m_Logger.error() << "Could not open file: " << filePath;
        setStatus(HTTP_INTERNAL_ERROR, "Internal Server Error");
        setBody("Internal Server Error");
        return;
    }

    // Read entire file
    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();

    // Set appropriate Content-Type
    std::string contentType = getContentType(filePath);
    setHeader("Content-Type", contentType);

    setBody(content);
    m_Logger.info() << "Loaded file: " << filePath << " (" << content.length() << " bytes, "
                    << contentType << ")";
}

void HttpResponse::appendBody(const std::string& content) {
    m_Body += content;

    // Update Content-Length
    std::ostringstream oss;
    oss << m_Body.length();
    setHeader("Content-Length", oss.str());
}

int HttpResponse::getStatusCode() const { return m_StatusCode; }

const std::string& HttpResponse::getStatusMessage() const { return m_StatusMessage; }

const std::string& HttpResponse::getHeader(const std::string& key) const {
    std::map<std::string, std::string>::const_iterator it = m_Headers.find(key);
    if (it != m_Headers.end()) {
        return it->second;
    }
    static const std::string empty;
    return empty;
}

const std::string& HttpResponse::getBody() const { return m_Body; }

std::size_t HttpResponse::getContentLength() const { return m_Body.length(); }

std::string HttpResponse::toString() const {
    std::ostringstream response;

    // Status line: HTTP/1.1 HTTP_OK OK
    response << "HTTP/1.1 " << m_StatusCode << " " << m_StatusMessage << "\r\n";

    // Headers
    for (std::map<std::string, std::string>::const_iterator it = m_Headers.begin();
         it != m_Headers.end(); ++it) {
        response << it->first << ": " << it->second << "\r\n";
    }

    // Empty line to separate headers from body
    response << "\r\n";

    // Body
    response << m_Body;

    return response.str();
}

void HttpResponse::clear() {
    m_StatusCode = HTTP_OK;
    m_StatusMessage = "OK";
    m_Headers.clear();
    m_Body.clear();
    setDefaultHeaders();
}

/* @------------------------------------------------------------------------@ */
/* |                           Static Methods                               | */
/* @------------------------------------------------------------------------@ */

HttpResponse HttpResponse::createOK(const std::string& body) {
    HttpResponse response(HTTP_OK);
    response.setHeader("Content-Type", "text/html; charset=utf-8");
    if (!body.empty()) {
        response.setBody(body);
    }
    return response;
}

HttpResponse HttpResponse::createNotFound(const std::string& message) {
    HttpResponse response(HTTP_NOT_FOUND);
    response.setHeader("Content-Type", "text/html; charset=utf-8");

    std::string body = message.empty()
                           ? "<!DOCTYPE html><html><head><title>HTTP_NOT_FOUND Not Found</title></head>"
                             "<body><h1>HTTP_NOT_FOUND Not Found</h1><p>The requested resource was not "
                             "found.</p></body></html>"
                           : message;

    response.setBody(body);
    return response;
}

HttpResponse HttpResponse::createInternalError(const std::string& message) {
    HttpResponse response(HTTP_INTERNAL_ERROR);
    response.setHeader("Content-Type", "text/html; charset=utf-8");

    std::string body =
        message.empty()
            ? "<!DOCTYPE html><html><head><title>HTTP_INTERNAL_ERROR Internal Server Error</title></head>"
              "<body><h1>HTTP_INTERNAL_ERROR Internal Server Error</h1><p>The server encountered an "
              "error.</p></body></html>"
            : message;

    response.setBody(body);
    return response;
}

HttpResponse HttpResponse::createBadRequest(const std::string& message) {
    HttpResponse response(HTTP_BAD_REQUEST);
    response.setHeader("Content-Type", "text/html; charset=utf-8");

    std::string body =
        message.empty()
            ? "<!DOCTYPE html><html><head><title>HTTP_BAD_REQUEST Bad Request</title></head>"
              "<body><h1>HTTP_BAD_REQUEST Bad Request</h1><p>The request was malformed.</p></body></html>"
            : message;

    response.setBody(body);
    return response;
}

HttpResponse HttpResponse::createMethodNotAllowed(const std::string& message) {
    HttpResponse response(HTTP_METHOD_NOT_ALLOWED);
    response.setHeader("Content-Type", "text/html; charset=utf-8");

    std::string body =
        message.empty() ? "<!DOCTYPE html><html><head><title>HTTP_METHOD_NOT_ALLOWED Method Not Allowed</title></head>"
                          "<body><h1>HTTP_METHOD_NOT_ALLOWED Method Not Allowed</h1><p>The requested method is not "
                          "allowed.</p></body></html>"
                        : message;

    response.setBody(body);
    return response;
}

/* @------------------------------------------------------------------------@ */
/* |                             Private Methods                            | */
/* @------------------------------------------------------------------------@ */

std::string HttpResponse::getDefaultStatusMessage(int statusCode) {
    switch (statusCode) {
        case HTTP_OK:
            return "OK";
        case HTTP_CREATED:
            return "Created";
        case HTTP_NO_CONTENT:
            return "No Content";
        case HTTP_MOVED_PERMANENTLY:
            return "Moved Permanently";
        case HTTP_FOUND:
            return "Found";
        case HTTP_BAD_REQUEST:
            return "Bad Request";
        case HTTP_UNAUTHORIZED:
            return "Unauthorized";
        case HTTP_FORBIDDEN:
            return "Forbidden";
        case HTTP_NOT_FOUND:
            return "Not Found";
        case HTTP_METHOD_NOT_ALLOWED:
            return "Method Not Allowed";
        case HTTP_PAYLOAD_TOO_LARGE:
            return "Payload Too Large";
        case HTTP_INTERNAL_ERROR:
            return "Internal Server Error";
        case HTTP_NOT_IMPLEMENTED:
            return "Not Implemented";
        case HTTP_VERSION_NOT_SUPPORTED:
            return "HTTP Version Not Supported";
        default:
            return "Unknown";
    }
}

std::string HttpResponse::getCurrentDateTime() {
    // For C++98 compatibility, we'll use a simple timestamp
    // In a real implementation, you might want to format this properly
    return "Mon, 27 Jan 2025 12:00:00 GMT";
}

std::string HttpResponse::toLowerCase(const std::string& str) {
    const int upperToLowerOffset = 32;
    std::string result = str;
    for (std::size_t i = 0; i < result.length(); ++i) {
        if (result[i] >= 'A' && result[i] <= 'Z') {
            result[i] = static_cast<char>(result[i] + upperToLowerOffset);
        }
    }
    return result;
}

void HttpResponse::setDefaultHeaders() {
    setHeader("Server", "webserv/1.0");
    setHeader("Date", getCurrentDateTime());
    setHeader("Connection", "close");
    setHeader("Content-Length", "0");
}

std::string HttpResponse::getContentType(const std::string& filePath) {
    // Find file extension
    std::size_t dotPos = filePath.rfind('.');
    if (dotPos == std::string::npos) {
        return "application/octet-stream";
    }

    std::string extension = toLowerCase(filePath.substr(dotPos));

    // Common MIME types
    if (extension == ".html" || extension == ".htm") {
        return "text/html; charset=utf-8";
    }
    if (extension == ".css") {
        return "text/css";
    }
    if (extension == ".js") {
        return "application/javascript";
    }
    if (extension == ".json") {
        return "application/json";
    }
    if (extension == ".xml") {
        return "application/xml";
    }
    if (extension == ".txt") {
        return "text/plain; charset=utf-8";
    }
    if (extension == ".jpg" || extension == ".jpeg") {
        return "image/jpeg";
    }
    if (extension == ".png") {
        return "image/png";
    }
    if (extension == ".gif") {
        return "image/gif";
    }
    if (extension == ".svg") {
        return "image/svg+xml";
    }
    if (extension == ".ico") {
        return "image/x-icon";
    }
    if (extension == ".pdf") {
        return "application/pdf";
    }
    if (extension == ".zip") {
        return "application/zip";
    }

    return "application/octet-stream";
}
