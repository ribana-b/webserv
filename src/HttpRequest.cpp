/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HttpRequest.cpp                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mancorte <mancorte@student.42malaga.com>   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/01/27 00:00:00 by mancorte          #+#    #+# Malaga      */
/*   Updated: 2025/01/27 00:00:00 by mancorte         ###   ########.com      */
/*                                                                            */
/* ************************************************************************** */

#include "HttpRequest.hpp"

#include <fstream>   // For std::ifstream
#include <iostream>  // For std::cout
#include <sstream>   // For std::istringstream
#include <string>    // For std::string, getline

#include "HttpServer.hpp"  // For HTTP_* constants
#include "Monitor.hpp"     // For MAX_URI_LENGTH, MAX_QUERY_STRING_LENGTH, MAX_HEADERS_SIZE

static std::size_t stringToNumber(const std::string& str) {
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

/* @------------------------------------------------------------------------@ */
/* |                        Constructor/Destructor                          | */
/* @------------------------------------------------------------------------@ */

HttpRequest::HttpRequest() :
    m_Logger(std::cout, false), m_IsComplete(false), m_IsValid(false), m_ErrorCode(0) {}

HttpRequest::HttpRequest(const Logger& logger) :
    m_Logger(logger), m_IsComplete(false), m_IsValid(false), m_ErrorCode(0) {}

HttpRequest::~HttpRequest() {}

HttpRequest::HttpRequest(const HttpRequest& that) :
    m_Logger(that.m_Logger),
    m_Method(that.m_Method),
    m_Path(that.m_Path),
    m_Version(that.m_Version),
    m_Headers(that.m_Headers),
    m_Body(that.m_Body),
    m_IsComplete(that.m_IsComplete),
    m_IsValid(that.m_IsValid),
    m_ErrorCode(that.m_ErrorCode),
    m_TempFilePath(that.m_TempFilePath) {}

HttpRequest& HttpRequest::operator=(const HttpRequest& that) {
    if (this != &that) {
        m_Method = that.m_Method;
        m_Path = that.m_Path;
        m_Version = that.m_Version;
        m_Headers = that.m_Headers;
        m_Body = that.m_Body;
        m_IsComplete = that.m_IsComplete;
        m_IsValid = that.m_IsValid;
        m_ErrorCode = that.m_ErrorCode;
        m_TempFilePath = that.m_TempFilePath;
    }
    return (*this);
}

/* @------------------------------------------------------------------------@ */
/* |                             Public Methods                             | */
/* @------------------------------------------------------------------------@ */

bool HttpRequest::parse(const std::string& rawData) {
    clear();

    std::size_t headerEnd = rawData.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        return false;
    }

    // Validate total headers size (prevent header flooding attacks)
    if (headerEnd > MAX_HEADERS_SIZE) {
        m_Logger.error() << "Headers too large: " << headerEnd << " bytes (max: "
                         << MAX_HEADERS_SIZE << ")";
        m_ErrorCode = HTTP_HEADER_FIELDS_TOO_LARGE;
        return false;
    }

    std::string        headerSection = rawData.substr(0, headerEnd);
    std::istringstream headerStream(headerSection);
    std::string        requestLine;

    if (!std::getline(headerStream, requestLine) || requestLine.empty()) {
        m_Logger.error() << "Failed to read request line";
        return false;
    }

    if (requestLine[requestLine.length() - 1] == '\r') {
        requestLine.erase(requestLine.length() - 1);
    }

    if (!parseRequestLine(requestLine)) {
        return false;
    }

    if (!parseHeaders(headerSection.substr(requestLine.length() + 2))) {
        return false;
    }

    if (!parseBody(rawData, headerEnd + 4)) {
        return false;
    }

    m_IsComplete = true;
    m_IsValid = true;
    return true;
}

bool HttpRequest::isComplete() const { return m_IsComplete; }

bool HttpRequest::isValid() const { return m_IsValid; }

void HttpRequest::clear() {
    m_Method.clear();
    m_Path.clear();
    m_Version.clear();
    m_Headers.clear();
    m_Body.clear();
    m_IsComplete = false;
    m_IsValid = false;
    m_ErrorCode = 0;
    // Don't clear m_TempFilePath as it may be set before parsing for large uploads
}

const std::string& HttpRequest::getMethod() const { return m_Method; }

const std::string& HttpRequest::getPath() const { return m_Path; }

const std::string& HttpRequest::getVersion() const { return m_Version; }

const std::string& HttpRequest::getHeader(const std::string& key) const {
    std::string                                        lowerKey = toLowerCase(key);
    std::map<std::string, std::string>::const_iterator it = m_Headers.find(lowerKey);
    if (it != m_Headers.end()) {
        return it->second;
    }
    static const std::string empty;
    return empty;
}

const std::string& HttpRequest::getBody() const { return m_Body; }

std::size_t HttpRequest::getContentLength() const {
    const std::string& contentLengthStr = getHeader("content-length");
    if (contentLengthStr.empty()) {
        return 0;
    }
    return stringToNumber(contentLengthStr);
}

int HttpRequest::getErrorCode() const { return m_ErrorCode; }

/* @------------------------------------------------------------------------@ */
/* |                             Private Methods                            | */
/* @------------------------------------------------------------------------@ */

bool HttpRequest::parseRequestLine(const std::string& line) {
    std::istringstream iss(line);
    std::string        method;
    std::string        path;
    std::string        version;

    if (!(iss >> method >> path >> version)) {
        m_Logger.error() << "Invalid request line format: " << line;
        return false;
    }

    if (!isValidMethod(method)) {
        m_Logger.error() << "Invalid HTTP method: " << method;
        return false;
    }

    if (path.empty() || path[0] != '/') {
        m_Logger.error() << "Invalid path: " << path;
        return false;
    }

    // Validate URI length (RFC 2616 recommends at least 2048 bytes)
    if (path.length() > MAX_URI_LENGTH) {
        m_Logger.error() << "URI too long: " << path.length() << " bytes (max: "
                         << MAX_URI_LENGTH << ")";
        m_ErrorCode = HTTP_URI_TOO_LONG;
        return false;
    }

    // Validate query string length (if present)
    std::size_t queryPos = path.find('?');
    if (queryPos != std::string::npos) {
        std::size_t queryLength = path.length() - queryPos - 1;
        if (queryLength > MAX_QUERY_STRING_LENGTH) {
            m_Logger.error() << "Query string too long: " << queryLength << " bytes (max: "
                             << MAX_QUERY_STRING_LENGTH << ")";
            m_ErrorCode = HTTP_URI_TOO_LONG;
            return false;
        }
    }

    if (!isValidVersion(version)) {
        m_Logger.error() << "Invalid HTTP version: " << version;
        return false;
    }

    m_Method = method;
    m_Path = path;
    m_Version = version;

    return true;
}

bool HttpRequest::parseHeaders(const std::string& headerSection) {
    std::istringstream headerStream(headerSection);
    std::string        line;

    while ((std::getline(headerStream, line) != 0) && !line.empty()) {
        if (line[line.length() - 1] == '\r') {
            line.erase(line.length() - 1);
        }

        if (line.empty()) {
            break;
        }

        std::size_t colonPos = line.find(':');
        if (colonPos == std::string::npos) {
            m_Logger.warn() << "Invalid header line (no colon): " << line;
            continue;
        }

        std::string key = trimWhitespace(line.substr(0, colonPos));
        std::string value = trimWhitespace(line.substr(colonPos + 1));

        if (key.empty()) {
            m_Logger.warn() << "Empty header key in line: " << line;
            continue;
        }

        m_Headers[toLowerCase(key)] = value;
    }

    return true;
}

bool HttpRequest::parseBody(const std::string& rawData, std::size_t headerEnd) {
    // Check for Transfer-Encoding: chunked
    std::string transferEncoding = getHeader("Transfer-Encoding");
    std::string lowerTE = toLowerCase(transferEncoding);

    const_cast<Logger&>(m_Logger).info()
        << "parseBody: Transfer-Encoding='" << transferEncoding << "' lowercase='" << lowerTE
        << "'";

    if (lowerTE.find("chunked") != std::string::npos) {
        // Handle chunked encoding
        // headerEnd already points to position after \r\n\r\n (passed from parse())
        std::size_t bodyStart = headerEnd;
        if (bodyStart >= rawData.length()) {
            m_Body = "";
            const_cast<Logger&>(m_Logger).info() << "parseBody: No body data after headers";
            return true;
        }

        std::string chunkedData = rawData.substr(bodyStart);
        const_cast<Logger&>(m_Logger).info()
            << "parseBody: Decoding chunked body, raw size: " << chunkedData.length();
        m_Body = decodeChunkedBody(chunkedData);
        const_cast<Logger&>(m_Logger).info()
            << "parseBody: Decoded body size: " << m_Body.length();
        return true;
    }

    // Normal Content-Length handling
    std::size_t contentLength = getContentLength();

    if (contentLength == 0) {
        return true;
    }

    // If we have a temp file, the body was already processed separately
    if (!m_TempFilePath.empty()) {
        return true;
    }

    if (headerEnd + contentLength > rawData.length()) {
        // For small bodies, allow incomplete data - it might be a request that will be rejected
        // anyway or the data might arrive in the next poll cycle
        std::size_t availableBodySize =
            rawData.length() > headerEnd ? rawData.length() - headerEnd : 0;
        m_Logger.warn() << "Incomplete body - expected " << contentLength << " bytes, got "
                        << availableBodySize << " bytes (continuing with partial data)";

        // Store whatever body data we have
        if (availableBodySize > 0) {
            m_Body = rawData.substr(headerEnd, availableBodySize);
        }
        return true;  // Continue processing - don't fail the entire request
    }

    m_Body = rawData.substr(headerEnd, contentLength);
    return true;
}

std::string HttpRequest::toLowerCase(const std::string& str) {
    const int   upperToLowerOffset = 32;
    std::string result = str;
    for (std::size_t i = 0; i < result.length(); ++i) {
        if (result[i] >= 'A' && result[i] <= 'Z') {
            result[i] = static_cast<char>(result[i] + upperToLowerOffset);
        }
    }
    return result;
}

std::string HttpRequest::trimWhitespace(const std::string& str) {
    std::size_t start = 0;
    std::size_t end = str.length();

    while (start < end && (str[start] == ' ' || str[start] == '\t')) {
        ++start;
    }

    while (end > start && (str[end - 1] == ' ' || str[end - 1] == '\t')) {
        --end;
    }

    return str.substr(start, end - start);
}

bool HttpRequest::isValidMethod(const std::string& method) {
    return (method == "GET" || method == "POST" || method == "DELETE" || method == "HEAD" ||
            method == "PUT" || method == "OPTIONS");
}

bool HttpRequest::isValidVersion(const std::string& version) {
    return (version == "HTTP/1.0" || version == "HTTP/1.1");
}

/* @------------------------------------------------------------------------@ */
/* |                        Large Upload Support Methods                    | */
/* @------------------------------------------------------------------------@ */

bool HttpRequest::hasLargeUpload() const { return !m_TempFilePath.empty(); }

const std::string& HttpRequest::getTempFilePath() const { return m_TempFilePath; }

void HttpRequest::setTempFilePath(const std::string& tempPath) { m_TempFilePath = tempPath; }

std::string HttpRequest::readBodyFromTempFile() const {
    if (m_TempFilePath.empty()) {
        const_cast<Logger&>(m_Logger).warn()
            << "HttpRequest: No temp file path set, returning empty body";
        return "";
    }

    std::ifstream file(m_TempFilePath.c_str(), std::ios::binary);
    if (!file.is_open()) {
        const_cast<Logger&>(m_Logger).error()
            << "HttpRequest: Failed to open temp file: " << m_TempFilePath;
        return "";
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    file.close();

    std::string content = buffer.str();
    return content;
}

/* @------------------------------------------------------------------------@ */
/* |                    Chunked Encoding Helper Methods                     | */
/* @------------------------------------------------------------------------@ */

std::size_t HttpRequest::hexToSize(const std::string& hex) {
    std::size_t result = 0;
    for (std::size_t i = 0; i < hex.length(); i++) {
        char c = hex[i];
        if (c >= '0' && c <= '9') {
            result = result * 16 + (c - '0');
        } else if (c >= 'a' && c <= 'f') {
            result = result * 16 + (c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            result = result * 16 + (c - 'A' + 10);
        } else {
            break;  // Stop on first non-hex character
        }
    }
    return result;
}

std::string HttpRequest::decodeChunkedBody(const std::string& chunkedData) {
    std::string decoded;
    std::size_t pos = 0;
    int         chunkCount = 0;

    while (pos < chunkedData.length()) {
        // Skip any leading whitespace/empty lines
        while (pos < chunkedData.length() &&
               (chunkedData[pos] == '\r' || chunkedData[pos] == '\n' || chunkedData[pos] == ' ')) {
            pos++;
        }

        if (pos >= chunkedData.length()) {
            break;
        }

        // Read chunk size (hex number until \r\n)
        std::size_t crlfPos = chunkedData.find("\r\n", pos);
        if (crlfPos == std::string::npos) {
            const_cast<Logger&>(m_Logger).warn()
                << "Chunk decode: No CRLF found at pos " << pos;
            break;
        }

        std::string sizeStr = chunkedData.substr(pos, crlfPos - pos);

        // Skip if size string is empty (another empty line)
        if (sizeStr.empty()) {
            pos = crlfPos + 2;
            continue;
        }

        std::size_t chunkSize = hexToSize(sizeStr);

        chunkCount++;
        if (chunkCount <= 3) {  // Log first 3 chunks
            const_cast<Logger&>(m_Logger).info() << "Chunk #" << chunkCount << ": size hex='"
                                                 << sizeStr << "' decimal=" << chunkSize;
        }

        if (chunkSize == 0) {
            const_cast<Logger&>(m_Logger).info()
                << "Chunk decode: Found terminator after " << chunkCount << " chunks, decoded "
                << decoded.length() << " bytes";
            break;
        }

        pos = crlfPos + 2;  // Skip \r\n after size

        // Read chunk data
        if (pos + chunkSize > chunkedData.length()) {
            const_cast<Logger&>(m_Logger).warn()
                << "Chunk decode: Incomplete chunk at pos " << pos;
            break;
        }

        decoded += chunkedData.substr(pos, chunkSize);
        pos += chunkSize + 2;  // Skip data and trailing \r\n
    }

    return decoded;
}
