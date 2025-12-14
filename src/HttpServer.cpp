/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HttpServer.cpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mancorte <mancorte@student.42malaga.com>   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/01/27 00:00:00 by mancorte          #+#    #+# Malaga      */
/*   Updated: 2025/01/27 00:00:00 by mancorte         ###   ########.com      */
/*                                                                            */
/* ************************************************************************** */

#include "HttpServer.hpp"
#include "UploadManager.hpp"  // For LARGE_FILE_THRESHOLD

#include <dirent.h>      // For directory operations
#include <fcntl.h>       // For open, O_NOFOLLOW
#include <netinet/in.h>  // For ntohs
#include <sys/stat.h>    // For stat
#include <sys/wait.h>    // For waitpid
#include <unistd.h>      // For access, unlink, fork, exec, pipe

#include <algorithm>  // For std::sort
#include <cerrno>     // For errno
#include <cstring>    // For strerror
#include <fstream>    // For std::ofstream
#include <iostream>   // For std::cout
#include <set>        // For std::set
#include <sstream>    // For std::ostringstream
#include <vector>     // For std::vector

// POSIX environ variable for passing environment to execve()
extern char **environ;

/* @------------------------------------------------------------------------@ */
/* |                        Constructor/Destructor                          | */
/* @------------------------------------------------------------------------@ */

HttpServer::HttpServer(const Config& config, const Logger& logger) :
    m_Config(config),
    m_Logger(logger),
    m_DocumentRoot("/var/www/html"),
    m_DefaultIndex("index.html") {
    m_Logger.info() << "HttpServer initialized with default document root: " << m_DocumentRoot;
}

HttpServer::~HttpServer() {}

HttpServer::HttpServer(const HttpServer& that) :
    m_Config(that.m_Config),
    m_Logger(that.m_Logger),
    m_DocumentRoot(that.m_DocumentRoot),
    m_DefaultIndex(that.m_DefaultIndex) {}

HttpServer& HttpServer::operator=(const HttpServer& that) {
    if (this != &that) {
        m_DocumentRoot = that.m_DocumentRoot;
        m_DefaultIndex = that.m_DefaultIndex;
    }
    return (*this);
}

/* @------------------------------------------------------------------------@ */
/* |                             Public Methods                             | */
/* @------------------------------------------------------------------------@ */

HttpResponse HttpServer::processRequest(const HttpRequest& request, int serverPort) {
    m_Logger.info() << "Processing " << request.getMethod() << " " << request.getPath() << " HTTP/"
                    << request.getVersion() << " on port " << serverPort;

    if (!request.isValid()) {
        m_Logger.warn() << "Invalid request received";
        return HttpResponse::createBadRequest();
    }

    const Config::Server* server = findMatchingServer(serverPort);
    if (server == 0) {
        m_Logger.error() << "No server configuration found for port " << serverPort;
        return HttpResponse::createInternalError("Server configuration error");
    }

    const std::string& method = request.getMethod();

    if (method == "GET") {
        return handleGET(request, *server);
    }
    if (method == "POST") {
        return handlePOST(request, *server);
    }
    if (method == "PUT") {
        return handlePUT(request, *server);
    }
    if (method == "DELETE") {
        return handleDELETE(request, *server);
    }
    if (method == "HEAD") {
        return handleHEAD(request, *server);
    }

    m_Logger.warn() << "Method not allowed: " << method;
    return createErrorResponse(HTTP_METHOD_NOT_ALLOWED, *server);
}

void HttpServer::setDocumentRoot(const std::string& root) { m_DocumentRoot = root; }

void HttpServer::setDefaultIndex(const std::string& index) { m_DefaultIndex = index; }

const std::string& HttpServer::getDocumentRoot() const { return m_DocumentRoot; }

const std::string& HttpServer::getDefaultIndex() const { return m_DefaultIndex; }

/* @------------------------------------------------------------------------@ */
/* |                             Private Methods                            | */
/* @------------------------------------------------------------------------@ */

HttpResponse HttpServer::handleGET(const HttpRequest& request, const Config::Server& server) {
    const std::string& requestPath = request.getPath();

    if (!isPathSafe(requestPath)) {
        m_Logger.warn() << "Unsafe path detected: " << requestPath;
        return createErrorResponse(HTTP_FORBIDDEN, server);
    }

    // Find matching location for this request
    const Config::Location* location = findMatchingLocation(server, requestPath);

    // Check if method is allowed for this location
    if ((location != 0) && !isMethodAllowed("GET", *location)) {
        m_Logger.warn() << "GET method not allowed for path: " << requestPath;
        return createErrorResponse(HTTP_METHOD_NOT_ALLOWED, server);
    }

    // Determine document root and index file
    std::string documentRoot;
    std::string indexFile = "index.html";

    if ((location != 0) && !location->root.empty()) {
        documentRoot = location->root;
        if (!location->index.empty()) {
            indexFile = location->index[0];
        }
    } else {
        documentRoot = server.root;
        if (!server.index.empty()) {
            indexFile = server.index[0];
        }
    }

    if (documentRoot.empty()) {
        documentRoot = "./html";
    }

    // Extract path without query string for file operations
    std::string cleanPath = requestPath;
    size_t      queryPos = cleanPath.find('?');
    if (queryPos != std::string::npos) {
        cleanPath = cleanPath.substr(0, queryPos);
    }

    // Construct file path - nginx-style: root + full URL path
    // e.g., root=./html, path=/upload/ -> ./html/upload/
    std::string filePath = documentRoot + cleanPath;

    struct stat fileStat;
    if (stat(filePath.c_str(), &fileStat) != 0) {
        return createErrorResponse(HTTP_NOT_FOUND, server);
    }

    m_Logger.info() << "GET " << requestPath << " -> filePath=" << filePath
                    << " isDir=" << (S_ISDIR(fileStat.st_mode) ? "YES" : "NO");
    std::cout.flush();

    if (S_ISREG(fileStat.st_mode)) {
        // Check if it's a CGI file
        if (isCGIFile(filePath)) {
            return handleCGI(request, server, filePath);
        }
        return serveStaticFile(filePath, server);
    }
    if (S_ISDIR(fileStat.st_mode)) {
        // Try to serve index file if configured
        if (!indexFile.empty()) {
            std::string indexPath = filePath;
            if (indexPath[indexPath.length() - 1] != '/') {
                indexPath += "/";
            }
            indexPath += indexFile;

            struct stat indexStat;
            if (stat(indexPath.c_str(), &indexStat) == 0 && S_ISREG(indexStat.st_mode)) {
                // Index file exists - serve it
                if (isCGIFile(indexPath)) {
                    return handleCGI(request, server, indexPath);
                }
                return serveStaticFile(indexPath, server);
            }
        }

        // Index file doesn't exist
        m_Logger.info() << "Directory without index file. requestPath='" << requestPath
                        << "' last_char='" << (requestPath.length() > 0 ? requestPath[requestPath.length() - 1] : '?') << "'";
        std::cout.flush();

        // If URL doesn't end with '/', return 404 (cannot serve directory without index and without explicit trailing slash)
        if (requestPath.length() > 0 && requestPath[requestPath.length() - 1] != '/') {
            m_Logger.warn() << "Directory requested without trailing slash and no index file: " << requestPath;
            std::cout.flush();
            return createErrorResponse(HTTP_NOT_FOUND, server);
        }

        // URL ends with '/' - check if autoindex is enabled
        if (location != 0 && location->autoindex) {
            return generateDirectoryListing(filePath, requestPath, server);
        }

        // autoindex is off - return 403 Forbidden
        m_Logger.warn() << "Directory listing disabled for: " << requestPath;
        return createErrorResponse(HTTP_FORBIDDEN, server);
    }
    return createErrorResponse(HTTP_FORBIDDEN, server);
}

HttpResponse HttpServer::handlePOST(const HttpRequest& request, const Config::Server& server) {
    const std::string& requestPath = request.getPath();

    m_Logger.info() << "POST request to " << requestPath << " (body: " << request.getBody().length()
                    << " bytes)";

    // Find matching location for this request
    const Config::Location* location = findMatchingLocation(server, requestPath);

    // Check if this is a CGI script request FIRST (before method validation)
    // CGI files have their own method requirements
    std::string cleanPath = requestPath;
    size_t      queryPos = cleanPath.find('?');
    if (queryPos != std::string::npos) {
        cleanPath = cleanPath.substr(0, queryPos);
    }

    // Determine document root and construct file path for CGI check
    std::string documentRoot = determinePOSTDocumentRoot(location, server);

    // Remove location prefix from request path if applicable
    std::string relativePath = cleanPath;
    if (location != 0 && !location->path.empty() && location->path != "/") {
        if (cleanPath.find(location->path) == 0) {
            relativePath = cleanPath.substr(location->path.length());
            if (relativePath.empty()) {
                relativePath = "/";
            }
        }
    }

    std::string filePath = documentRoot + relativePath;

    // If it's a CGI file, handle it as CGI (bypass method validation)
    if (isCGIFile(filePath)) {
        struct stat fileStat;
        if (stat(filePath.c_str(), &fileStat) == 0 && S_ISREG(fileStat.st_mode)) {
            return handleCGI(request, server, filePath);
        }
        // CGI file requested but doesn't exist -> 404
        // (File existence takes precedence over method restrictions)
        m_Logger.warn() << "CGI file not found: " << filePath;
        return createErrorResponse(HTTP_NOT_FOUND, server);
    }

    // Not a CGI file - validate POST request parameters (including method check)
    HttpResponse validationResponse = validatePOSTRequest(request, server, location, requestPath);
    if (validationResponse.getStatusCode() != HTTP_OK) {
        return validationResponse;
    }

    // Handle file upload or regular POST processing
    return handleFileUpload(request, server, requestPath);
}

// Helper method to validate POST request parameters
HttpResponse HttpServer::validatePOSTRequest(const HttpRequest&      request,
                                             const Config::Server&   server,
                                             const Config::Location* location,
                                             const std::string&      requestPath) {
    // Check if method is allowed for this location
    if ((location != 0) && !isMethodAllowed("POST", *location)) {
        m_Logger.warn() << "POST method not allowed for path: " << requestPath;
        return createErrorResponse(HTTP_METHOD_NOT_ALLOWED, server);
    }

    // Check client body size limit
    if ((location != 0) && location->clientMaxBodySize > 0 &&
        request.getBody().length() > location->clientMaxBodySize) {
        m_Logger.warn() << "Request body too large: " << request.getBody().length() << " > "
                        << location->clientMaxBodySize;
        return createErrorResponse(HTTP_PAYLOAD_TOO_LARGE, server);
    }

    // Return empty response if validation passes (we'll use isEmpty check)
    return HttpResponse();
}

// Helper method to determine document root for POST requests
std::string HttpServer::determinePOSTDocumentRoot(const Config::Location* location,
                                                  const Config::Server&   server) {
    std::string documentRoot;
    if ((location != 0) && !location->root.empty()) {
        documentRoot = location->root;
    } else {
        documentRoot = server.root;
    }
    if (documentRoot.empty()) {
        documentRoot = "./html";
    }
    return documentRoot;
}

// Helper method to process large file uploads
bool HttpServer::processLargeFileUpload(const HttpRequest& request, const std::string& filename,
                                        std::size_t& fileSize) {
    // Check if temp file exists
    std::ifstream tempCheck(request.getTempFilePath().c_str());
    if (!tempCheck.good()) {
        m_Logger.error() << "Temp file does not exist or is not readable: "
                         << request.getTempFilePath();
        return false;
    }
    tempCheck.close();

    // Copy file using C++ streams (rename not in allowed functions)
    std::ifstream source(request.getTempFilePath().c_str(), std::ios::binary);
    if (source.is_open()) {
        std::ofstream dest(filename.c_str(), std::ios::binary);
        if (dest.is_open()) {
            dest << source.rdbuf();
            source.close();
            dest.close();

            // Verify copy was successful and get file size
            std::ifstream verify(filename.c_str(), std::ios::binary | std::ios::ate);
            if (verify.is_open()) {
                fileSize = static_cast<std::size_t>(verify.tellg());
                verify.close();

                // Delete original temp file
                unlink(request.getTempFilePath().c_str());
                m_Logger.info() << "Large file copied successfully from "
                                << request.getTempFilePath() << " to " << filename << " ("
                                << fileSize << " bytes)";
                return true;
            }
            m_Logger.error() << "Failed to verify copied file: " << filename;
        } else {
            source.close();
            m_Logger.error() << "Failed to open destination file for writing: " << filename;
        }
    } else {
        m_Logger.error() << "Failed to open temp file for reading: " << request.getTempFilePath();
    }
    return false;
}

// Helper function to extract file content from multipart/form-data body
// Returns the actual file content without multipart headers/boundaries
static std::string extractMultipartFileContent(const std::string& body, const std::string& boundary) {
    if (boundary.empty() || body.empty()) {
        return body;  // Not multipart, return as-is
    }

    // Multipart format:
    // --boundary\r\n
    // Content-Disposition: form-data; name="file"; filename="test.txt"\r\n
    // Content-Type: text/plain\r\n
    // \r\n
    // [FILE CONTENT]
    // \r\n--boundary--

    std::string startBoundary = "--" + boundary;
    std::string endBoundary = "\r\n--" + boundary;

    // Find the start boundary
    std::size_t boundaryStart = body.find(startBoundary);
    if (boundaryStart == std::string::npos) {
        return body;  // No boundary found, return as-is
    }

    // Find the end of headers (double CRLF after boundary line)
    std::size_t headersEnd = body.find("\r\n\r\n", boundaryStart);
    if (headersEnd == std::string::npos) {
        return body;  // Malformed multipart
    }
    std::size_t contentStart = headersEnd + 4;  // Skip \r\n\r\n

    // Find the end boundary
    std::size_t contentEnd = body.find(endBoundary, contentStart);
    if (contentEnd == std::string::npos) {
        // Try alternative: content might go to end with just --boundary--
        contentEnd = body.find("--" + boundary + "--", contentStart);
        if (contentEnd == std::string::npos) {
            // No end boundary, take everything after headers
            return body.substr(contentStart);
        }
        // Remove potential \r\n before final boundary
        if (contentEnd >= 2 && body.substr(contentEnd - 2, 2) == "\r\n") {
            contentEnd -= 2;
        }
    }

    if (contentEnd <= contentStart) {
        return "";  // Empty file
    }

    return body.substr(contentStart, contentEnd - contentStart);
}

// Helper function to extract original filename from multipart body
static std::string extractFilenameFromMultipart(const std::string& body, const std::string& boundary) {
    if (boundary.empty() || body.empty()) {
        return "";
    }

    // Find Content-Disposition header
    std::size_t dispPos = body.find("Content-Disposition:");
    if (dispPos == std::string::npos) {
        dispPos = body.find("content-disposition:");
    }
    if (dispPos == std::string::npos) {
        return "";
    }

    // Find filename="xxx" in the header
    std::size_t filenamePos = body.find("filename=\"", dispPos);
    if (filenamePos == std::string::npos) {
        return "";
    }

    std::size_t start = filenamePos + 10;  // Length of 'filename="'
    std::size_t end = body.find("\"", start);
    if (end == std::string::npos || end <= start) {
        return "";
    }

    std::string filename = body.substr(start, end - start);

    // Security: remove any path components (keep only basename)
    std::size_t lastSlash = filename.rfind('/');
    if (lastSlash != std::string::npos) {
        filename = filename.substr(lastSlash + 1);
    }
    lastSlash = filename.rfind('\\');
    if (lastSlash != std::string::npos) {
        filename = filename.substr(lastSlash + 1);
    }

    // Security: remove dangerous characters
    std::string safe;
    for (std::size_t i = 0; i < filename.length(); ++i) {
        char c = filename[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-') {
            safe += c;
        }
    }

    return safe.empty() ? "" : safe;
}

// Helper function to extract boundary from Content-Type header
static std::string extractBoundaryFromContentType(const std::string& contentType) {
    std::size_t boundaryPos = contentType.find("boundary=");
    if (boundaryPos == std::string::npos) {
        return "";
    }

    std::size_t start = boundaryPos + 9;  // Length of "boundary="
    std::size_t end = contentType.length();

    // Handle quoted boundary
    if (start < contentType.length() && contentType[start] == '"') {
        start++;
        end = contentType.find('"', start);
        if (end == std::string::npos) {
            end = contentType.length();
        }
    } else {
        // Find end (semicolon, space, or end of string)
        end = contentType.find_first_of("; \r\n", start);
        if (end == std::string::npos) {
            end = contentType.length();
        }
    }

    return contentType.substr(start, end - start);
}

// Helper method to process regular file uploads
bool HttpServer::processRegularFileUpload(const HttpRequest& request, const std::string& filename,
                                          std::size_t& fileSize) {
    std::string body = request.getBody();
    if (body.empty()) {
        m_Logger.warn() << "Empty upload request body";
        return false;
    }

    // Check if this is a multipart/form-data upload and extract actual file content
    std::string contentType = request.getHeader("Content-Type");
    if (contentType.find("multipart/form-data") != std::string::npos) {
        std::string boundary = extractBoundaryFromContentType(contentType);
        if (!boundary.empty()) {
            m_Logger.info() << "Parsing multipart upload with boundary: " << boundary;
            body = extractMultipartFileContent(body, boundary);
            if (body.empty()) {
                m_Logger.warn() << "Failed to extract file content from multipart body";
                return false;
            }
            m_Logger.info() << "Extracted " << body.size() << " bytes of file content from multipart";
        }
    }

    std::ofstream outFile(filename.c_str(), std::ios::binary);
    if (outFile.is_open()) {
        outFile.write(body.c_str(), static_cast<std::streamsize>(body.size()));
        outFile.close();
        fileSize = body.length();
        m_Logger.info() << "Small file uploaded successfully: " << filename << " (" << fileSize
                        << " bytes)";
        return true;
    }
    return false;
}

// Helper method to handle file upload logic
HttpResponse HttpServer::handleFileUpload(const HttpRequest& request, const Config::Server& server,
                                          const std::string& requestPath) {
    // Accept any path starting with /upload (e.g., /upload, /upload_small, /upload_large)
    if (requestPath.find("/upload") != 0) {
        // Default POST response for non-upload requests
        HttpResponse response(HTTP_OK, m_Logger);
        response.setHeader("Content-Type", "text/plain");
        response.setBody("POST request processed successfully");
        return response;
    }

    bool isLargeUpload = request.hasLargeUpload();

    // Validate request body for regular uploads
    if (!isLargeUpload && request.getBody().empty()) {
        m_Logger.warn() << "Empty upload request body";
        return createErrorResponse(HTTP_BAD_REQUEST, server);
    }

    // Get documentRoot from config for this location
    const Config::Location* location = findMatchingLocation(server, requestPath);
    std::string documentRoot = determinePOSTDocumentRoot(location, server);

    // Try to extract original filename from multipart data
    std::string originalFilename;
    std::string contentType = request.getHeader("Content-Type");
    if (contentType.find("multipart/form-data") != std::string::npos) {
        if (isLargeUpload) {
            // For large uploads, filename was extracted during streaming by UploadManager
            originalFilename = request.getOriginalFilename();
        } else {
            // For small uploads, extract from body in memory
            std::string boundary = extractBoundaryFromContentType(contentType);
            if (!boundary.empty()) {
                originalFilename = extractFilenameFromMultipart(request.getBody(), boundary);
            }
        }
    }

    // Generate final filename
    std::ostringstream  oss;
    static unsigned int uploadCounter = 0;
    ++uploadCounter;

    if (!originalFilename.empty()) {
        // Use original filename (already sanitized)
        oss << documentRoot << "/upload/" << originalFilename;
    } else {
        // Fallback to generated name
        oss << documentRoot << "/upload/uploaded_" << uploadCounter;
        if (isLargeUpload) {
            oss << ".bin";
        } else {
            oss << ".txt";
        }
    }
    std::string filename = oss.str();

    bool        success = false;
    std::size_t fileSize = 0;

    if (isLargeUpload) {
        m_Logger.info() << "Processing large upload from temp file: " << request.getTempFilePath();
        success = processLargeFileUpload(request, filename, fileSize);
    } else {
        success = processRegularFileUpload(request, filename, fileSize);
    }

    if (success) {
        std::ostringstream responseBody;
        responseBody << "<h1>Upload Successful!</h1><p>File saved as: " << filename
                     << "</p><p>Size: " << fileSize << " bytes</p><p>Type: "
                     << (isLargeUpload ? "Large file (streamed to disk)" : "Small file (in memory)")
                     << "</p>";

        HttpResponse response(HTTP_OK, m_Logger);
        response.setHeader("Content-Type", "text/html");
        response.setBody(responseBody.str());
        return response;
    }

    m_Logger.error() << "Failed to save uploaded file: " << filename;
    return createErrorResponse(HTTP_INTERNAL_ERROR, server);
}

HttpResponse HttpServer::handlePUT(const HttpRequest& request, const Config::Server& server) {
    const std::string& requestPath = request.getPath();

    m_Logger.info() << "PUT request to " << requestPath;

    if (!isPathSafe(requestPath)) {
        m_Logger.warn() << "Unsafe path detected in PUT: " << requestPath;
        return createErrorResponse(HTTP_FORBIDDEN, server);
    }

    // Find matching location for this request
    const Config::Location* location = findMatchingLocation(server, requestPath);

    // Check if method is allowed for this location
    if ((location != 0) && !isMethodAllowed("PUT", *location)) {
        m_Logger.warn() << "PUT method not allowed for path: " << requestPath;
        return createErrorResponse(HTTP_METHOD_NOT_ALLOWED, server);
    }

    // Determine document root
    std::string documentRoot;
    if ((location != 0) && !location->root.empty()) {
        documentRoot = location->root;
    } else {
        documentRoot = server.root;
    }

    if (documentRoot.empty()) {
        documentRoot = "./html";
    }

    // Construct file path - remove location prefix from request path if applicable
    std::string relativePath = requestPath;
    if (location != 0 && !location->path.empty() && location->path != "/") {
        // Remove location path prefix from request path
        if (requestPath.find(location->path) == 0) {
            relativePath = requestPath.substr(location->path.length());
            if (relativePath.empty()) {
                relativePath = "/";
            }
        }
    }

    std::string filePath = documentRoot + relativePath;

    // Write the request body to the file
    std::ofstream outFile(filePath.c_str(), std::ios::binary | std::ios::trunc);
    if (!outFile) {
        m_Logger.error() << "Failed to open file for writing: " << filePath;
        return createErrorResponse(HTTP_INTERNAL_ERROR, server);
    }

    const std::string& body = request.getBody();
    outFile.write(body.c_str(), body.length());
    outFile.close();

    if (!outFile.good()) {
        m_Logger.error() << "Failed to write to file: " << filePath;
        return createErrorResponse(HTTP_INTERNAL_ERROR, server);
    }

    m_Logger.info() << "File saved successfully: " << filePath << " (" << body.length() << " bytes)";

    HttpResponse response(HTTP_CREATED, m_Logger);
    response.setHeader("Content-Type", "text/html");
    response.setBody("<h1>Upload Successful!</h1><p>File saved: " + requestPath + "</p>");
    return response;
}

HttpResponse HttpServer::handleDELETE(const HttpRequest& request, const Config::Server& server) {
    const std::string& requestPath = request.getPath();

    m_Logger.info() << "DELETE request to " << requestPath;

    if (!isPathSafe(requestPath)) {
        m_Logger.warn() << "Unsafe path detected in DELETE: " << requestPath;
        return createErrorResponse(HTTP_FORBIDDEN, server);
    }

    // Find matching location for this request
    const Config::Location* location = findMatchingLocation(server, requestPath);

    // Check if method is allowed for this location
    if ((location != 0) && !isMethodAllowed("DELETE", *location)) {
        m_Logger.warn() << "DELETE method not allowed for path: " << requestPath;
        return createErrorResponse(HTTP_METHOD_NOT_ALLOWED, server);
    }

    // Determine document root
    std::string documentRoot;
    if ((location != 0) && !location->root.empty()) {
        documentRoot = location->root;
    } else {
        documentRoot = server.root;
    }

    if (documentRoot.empty()) {
        documentRoot = "./html";
    }

    // Construct file path - nginx-style: root + full URL path
    std::string filePath = documentRoot + requestPath;

    struct stat fileStat;
    if (stat(filePath.c_str(), &fileStat) != 0) {
        return createErrorResponse(HTTP_NOT_FOUND, server);
    }

    if (S_ISREG(fileStat.st_mode)) {
        if (unlink(filePath.c_str()) == 0) {
            m_Logger.info() << "File deleted successfully: " << filePath;

            HttpResponse response(HTTP_OK, m_Logger);
            response.setHeader("Content-Type", "text/html");
            response.setBody("<h1>Delete Successful!</h1><p>File deleted: " + requestPath + "</p>");
            return response;
        }

        m_Logger.error() << "Failed to delete file: " << filePath;
        return createErrorResponse(HTTP_INTERNAL_ERROR, server);
    }
    m_Logger.warn() << "Cannot delete non-regular file: " << filePath;
    return createErrorResponse(HTTP_FORBIDDEN, server);
}

HttpResponse HttpServer::handleHEAD(const HttpRequest& request, const Config::Server& server) {
    const std::string& requestPath = request.getPath();

    // Find matching location for this request
    const Config::Location* location = findMatchingLocation(server, requestPath);

    // Validate HEAD request parameters
    HttpResponse validationResponse = validateHEADRequest(request, server, location, requestPath);
    if (validationResponse.getStatusCode() != HTTP_OK) {
        return validationResponse;
    }

    // Determine document root and index file
    std::string indexFile;
    std::string documentRoot = determineHEADDocumentRoot(location, server, indexFile);

    // Remove location prefix from request path if applicable
    std::string relativePath = requestPath;
    if (location != 0 && !location->path.empty() && location->path != "/") {
        if (requestPath.find(location->path) == 0) {
            relativePath = requestPath.substr(location->path.length());
            if (relativePath.empty()) {
                relativePath = "/";
            }
        }
    }

    // Construct and validate file path
    std::string filePath = constructHEADFilePath(documentRoot, relativePath, indexFile);
    if (filePath.empty()) {
        return createErrorResponse(HTTP_URI_TOO_LONG, server);
    }

    // Check if file exists and get stats
    struct stat fileStat;
    if (stat(filePath.c_str(), &fileStat) != 0) {
        return createErrorResponse(HTTP_NOT_FOUND, server);
    }

    if (S_ISREG(fileStat.st_mode)) {
        HttpResponse response(HTTP_OK, m_Logger);

        // Determine content type and set headers
        std::string contentType = determineContentTypeFromPath(filePath);
        response.setHeader("Content-Type", contentType);

        std::ostringstream oss;
        oss << fileStat.st_size;
        response.setHeader("Content-Length", oss.str());

        return response;
    }

    if (S_ISDIR(fileStat.st_mode)) {
        HttpResponse response(HTTP_OK, m_Logger);
        response.setHeader("Content-Type", "text/html");
        return response;
    }

    return createErrorResponse(HTTP_FORBIDDEN, server);
}

// Helper method to validate HEAD request parameters
HttpResponse HttpServer::validateHEADRequest(const HttpRequest& /* request */,
                                             const Config::Server&   server,
                                             const Config::Location* location,
                                             const std::string&      requestPath) {
    if (!isPathSafe(requestPath)) {
        m_Logger.warn() << "Unsafe path detected: " << requestPath;
        return createErrorResponse(HTTP_FORBIDDEN, server);
    }

    // Check if method is allowed for this location
    if ((location != 0) && !isMethodAllowed("HEAD", *location)) {
        m_Logger.warn() << "HEAD method not allowed for path: " << requestPath;
        return createErrorResponse(HTTP_METHOD_NOT_ALLOWED, server);
    }

    // Return empty response if validation passes
    return HttpResponse();
}

// Helper method to determine document root and index file for HEAD requests
std::string HttpServer::determineHEADDocumentRoot(const Config::Location* location,
                                                  const Config::Server&   server,
                                                  std::string&            indexFile) {
    std::string documentRoot;
    indexFile = "index.html";

    if ((location != 0) && !location->root.empty()) {
        documentRoot = location->root;
        if (!location->index.empty()) {
            indexFile = location->index[0];
        }
    } else {
        documentRoot = server.root;
        if (!server.index.empty()) {
            indexFile = server.index[0];
        }
    }

    if (documentRoot.empty()) {
        documentRoot = "./html";
    }

    if (documentRoot.empty() || documentRoot.length() > HTTP_INTERNAL_ERROR) {
        m_Logger.warn() << "HEAD: Invalid server.root, using fallback";
        documentRoot = "./html";
    }

    // Validate index file from server config
    if (!server.index.empty()) {
        indexFile = server.index[0];
        if (indexFile.length() > MIN_PATH_LENGTH || indexFile.find("..") != std::string::npos) {
            m_Logger.warn() << "HEAD: Invalid index file name, using default";
            indexFile = "index.html";
        }
    }

    return documentRoot;
}

// Helper method to construct and validate file path for HEAD requests
std::string HttpServer::constructHEADFilePath(const std::string& documentRoot,
                                              const std::string& requestPath,
                                              const std::string& indexFile) {
    // Validate path lengths before concatenation
    if (documentRoot.length() + requestPath.length() > MAX_PATH_LENGTH) {
        m_Logger.error() << "HEAD: Combined path would be too long";
        return "";
    }

    std::string filePath;
    try {
        if (requestPath == "/") {
            filePath = documentRoot + "/" + indexFile;
        } else {
            filePath = documentRoot + requestPath;
        }

        if (filePath.length() > MAX_FILE_SIZE_MB) {
            m_Logger.error() << "HEAD: Final filePath too long: " << filePath.length();
            return "";
        }
    } catch (const std::exception& e) {
        m_Logger.error() << "HEAD: String concatenation failed: " << e.what();
        return "";
    }

    return filePath;
}

// Helper method to determine content type from file path
std::string HttpServer::determineContentTypeFromPath(const std::string& filePath) {
    std::string contentType = "application/octet-stream";
    std::size_t dotPos = filePath.find_last_of('.');

    if (dotPos != std::string::npos) {
        std::string extension = filePath.substr(dotPos);
        if (extension == ".html" || extension == ".htm") {
            contentType = "text/html; charset=utf-8";
        } else if (extension == ".css") {
            contentType = "text/css";
        } else if (extension == ".js") {
            contentType = "application/javascript";
        } else if (extension == ".txt") {
            contentType = "text/plain; charset=utf-8";
        } else if (extension == ".jpg" || extension == ".jpeg") {
            contentType = "image/jpeg";
        } else if (extension == ".png") {
            contentType = "image/png";
        }
    }

    return contentType;
}

HttpResponse HttpServer::serveStaticFile(const std::string&    filePath,
                                         const Config::Server& server) {
    // Security check: Detect and reject symbolic links using O_NOFOLLOW
    int testFd = open(filePath.c_str(), O_RDONLY | O_NOFOLLOW);
    if (testFd == -1) {
        // Check if it failed because it's a symlink
        if (errno == ELOOP) {
            m_Logger.warn() << "Symbolic link rejected for security reasons: " << filePath;
            return createErrorResponse(HTTP_FORBIDDEN, server);
        }
        // Check if it failed because of permission denied
        if (errno == EACCES) {
            m_Logger.warn() << "No read permission for file: " << filePath;
            return createErrorResponse(HTTP_FORBIDDEN, server);
        }
        // Other error (file doesn't exist, etc.)
        return createErrorResponse(HTTP_NOT_FOUND, server);
    }
    close(testFd);

    // Check if file exists and get stats
    struct stat fileStat;
    if (stat(filePath.c_str(), &fileStat) != 0) {
        return createErrorResponse(HTTP_NOT_FOUND, server);
    }

    // Verify it's a regular file
    if (!S_ISREG(fileStat.st_mode)) {
        m_Logger.warn() << "Not a regular file: " << filePath;
        return createErrorResponse(HTTP_FORBIDDEN, server);
    }

    // Check read permissions
    if (access(filePath.c_str(), R_OK) != 0) {
        m_Logger.warn() << "No read permission for file: " << filePath;
        return createErrorResponse(HTTP_FORBIDDEN, server);
    }

    // Check file size (basic limit of 100MB for safety)
    const std::size_t maxFileSize = static_cast<std::size_t>(100) * BYTES_PER_KB * BYTES_PER_KB;
    if (static_cast<std::size_t>(fileStat.st_size) > maxFileSize) {
        m_Logger.warn() << "File too large: " << filePath << " (" << fileStat.st_size << " bytes)";
        return createErrorResponse(HTTP_PAYLOAD_TOO_LARGE, server);
    }

    HttpResponse response(HTTP_OK, m_Logger);
    response.setBodyFromFile(filePath);

    // Double-check that file loading succeeded
    if (response.getStatusCode() != HTTP_OK) {
        m_Logger.error() << "Failed to load file content: " << filePath;
        return createErrorResponse(HTTP_INTERNAL_ERROR, server);
    }

    m_Logger.info() << "Served file: " << filePath << " (" << response.getContentLength()
                    << " bytes)";
    return response;
}

HttpResponse HttpServer::generateDirectoryListing(const std::string&    dirPath,
                                                  const std::string&    requestPath,
                                                  const Config::Server& server) {
    (void)server;

    HttpResponse response(HTTP_OK, m_Logger);
    response.setHeader("Content-Type", "text/html; charset=utf-8");

    // Collect directory entries using helper method
    std::vector<std::string> directories;
    std::vector<std::string> files;

    if (!collectDirectoryEntries(dirPath, directories, files)) {
        return createErrorResponse(HTTP_FORBIDDEN, server);
    }

    // Generate complete HTML using helper method
    std::string htmlContent = generateDirectoryHTML(RequestPath(requestPath), DirPath(dirPath),
                                                    DirectoryList(directories), FileList(files));
    response.setBody(htmlContent);

    m_Logger.info() << "Generated directory listing for: " << requestPath << " ("
                    << directories.size() << " dirs, " << files.size() << " files)";
    return response;
}

// Helper method to collect directory entries
bool HttpServer::collectDirectoryEntries(const std::string&        dirPath,
                                         std::vector<std::string>& directories,
                                         std::vector<std::string>& files) {
    DIR* dir = opendir(dirPath.c_str());
    if (dir == 0) {
        m_Logger.warn() << "Cannot open directory: " << dirPath;
        return false;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        std::string name = entry->d_name;

        // Skip . and .. but allow hidden files starting with .
        if (name == "." || name == "..") {
            continue;
        }

        // dirPath is the base directory, name is the file/directory name to append
        std::string fullPath = joinPath(dirPath, name);
        struct stat entryStat;

        if (stat(fullPath.c_str(), &entryStat) == 0) {
            if (S_ISDIR(entryStat.st_mode)) {
                directories.push_back(name);
            } else {
                files.push_back(name);
            }
        }
    }
    closedir(dir);

    // Sort entries alphabetically
    std::sort(directories.begin(), directories.end());
    std::sort(files.begin(), files.end());

    return true;
}

// Helper method to format file size for display
std::string HttpServer::formatFileSize(off_t fileSize) {
    std::ostringstream size;
    if (fileSize < BYTES_PER_KB) {
        size << fileSize << "B";
    } else if (fileSize < static_cast<long>(BYTES_PER_KB) * BYTES_PER_KB) {
        size << (fileSize / BYTES_PER_KB) << "KB";
    } else {
        size << (fileSize / (static_cast<long>(BYTES_PER_KB) * BYTES_PER_KB)) << "MB";
    }
    return size.str();
}

// Helper method to generate HTML header and styles
std::string HttpServer::generateHTMLHeader(const std::string& requestPath) {
    std::ostringstream html;
    html << "<!DOCTYPE html>\n";
    html << "<html><head>\n";
    html << "<title>Directory listing for " << requestPath << "</title>\n";
    html << "<style>\n";
    html << "  body { font-family: monospace; margin: 40px; }\n";
    html << "  h1 { color: #333; border-bottom: 1px solid #ccc; padding-bottom: 10px; }\n";
    html << "  .directory { color: #0066cc; font-weight: bold; }\n";
    html << "  .file { color: #000; }\n";
    html << "  a { text-decoration: none; display: block; padding: 2px 0; }\n";
    html << "  a:hover { background-color: #f0f0f0; }\n";
    html << "  .size { color: #666; float: right; }\n";
    html << "</style>\n";
    html << "</head><body>\n";
    html << "<h1>Directory listing for " << requestPath << "</h1>\n";
    return html.str();
}

// Helper method to generate parent directory link
std::string HttpServer::generateParentDirectoryLink(const std::string& requestPath) {
    if (requestPath == "/") {
        return "";
    }

    std::string parentPath = requestPath;
    if (parentPath.length() > 1 && parentPath[parentPath.length() - 1] == '/') {
        parentPath = parentPath.substr(0, parentPath.length() - 1);
    }
    std::size_t lastSlash = parentPath.rfind('/');
    if (lastSlash != std::string::npos) {
        parentPath = parentPath.substr(0, lastSlash);
        if (parentPath.empty()) {
            parentPath = "/";
        }
    }

    std::ostringstream html;
    html << "<a href=\"" << parentPath << "\" class=\"directory\">[Parent Directory]</a>\n";
    return html.str();
}

// Helper method to generate directory entries
std::string HttpServer::generateDirectoryEntries(const RequestPath&   requestPath,
                                                 const DirPath&       dirPath,
                                                 const DirectoryList& directories) {
    std::ostringstream html;

    for (std::vector<std::string>::const_iterator it = directories.value.begin();
         it != directories.value.end(); ++it) {
        std::string linkPath = requestPath.value;
        if (linkPath[linkPath.length() - 1] != '/') {
            linkPath += "/";
        }
        linkPath += *it;

        std::string fullPath = joinPath(dirPath.value, *it);
        struct stat entryStat;
        std::string sizeInfo;
        if (stat(fullPath.c_str(), &entryStat) == 0) {
            sizeInfo = "<span class=\"size\">[DIR]</span>";
        }

        html << "<a href=\"" << linkPath << "/\" class=\"directory\">" << *it << "/" << sizeInfo
             << "</a>\n";
    }

    return html.str();
}

// Helper method to generate file entries
std::string HttpServer::generateFileEntries(const RequestPath& requestPath, const DirPath& dirPath,
                                            const FileList& files) {
    std::ostringstream html;

    for (std::vector<std::string>::const_iterator it = files.value.begin(); it != files.value.end();
         ++it) {
        std::string linkPath = requestPath.value;
        if (linkPath[linkPath.length() - 1] != '/') {
            linkPath += "/";
        }
        linkPath += *it;

        std::string fullPath = joinPath(dirPath.value, *it);
        struct stat entryStat;
        std::string sizeInfo;
        if (stat(fullPath.c_str(), &entryStat) == 0) {
            sizeInfo = "<span class=\"size\">" + formatFileSize(entryStat.st_size) + "</span>";
        }

        html << "<a href=\"" << linkPath << "\" class=\"file\">" << *it << sizeInfo << "</a>\n";
    }

    return html.str();
}

// Helper method to generate complete directory HTML
std::string HttpServer::generateDirectoryHTML(const RequestPath&   requestPath,
                                              const DirPath&       dirPath,
                                              const DirectoryList& directories,
                                              const FileList&      files) {
    std::ostringstream html;

    html << generateHTMLHeader(requestPath.value);
    html << generateParentDirectoryLink(requestPath.value);
    html << generateDirectoryEntries(requestPath, dirPath, directories);
    html << generateFileEntries(requestPath, dirPath, files);
    html << "</body></html>";

    return html.str();
}

const Config::Server* HttpServer::findMatchingServer(int port) const {
    const std::vector<Config::Server>& servers = m_Config.getServers();

    if (servers.empty()) {
        m_Logger.warn() << "findMatchingServer: no servers configured";
        return NULL;
    }

    for (std::vector<Config::Server>::const_iterator it = servers.begin(); it != servers.end();
         ++it) {
        const Config::Server& server = *it;

        const std::vector<Config::Listen>& listens = server.listens;
        for (std::vector<Config::Listen>::const_iterator listenIt = listens.begin();
             listenIt != listens.end(); ++listenIt) {
            int serverPort = ntohs(listenIt->second);

            if (serverPort == port) {
                return &server;
            }
        }
    }

    m_Logger.warn() << "findMatchingServer: no server found for port " << port;
    return NULL;
}

const Config::Location* HttpServer::findMatchingLocation(const Config::Server& server,
                                                         const std::string&    path) {
    // Check if server object is valid
    try {
        const std::vector<Config::Location>& locations = server.locations;

        // Check if we can safely access the vector
        std::size_t locationCount = locations.size();
        if (locationCount == 0) {
            return NULL;
        }

        const Config::Location* bestMatch = NULL;
        std::size_t             bestMatchLength = 0;

        // Safe iteration with bounds checking
        for (std::size_t i = 0; i < locationCount; ++i) {
            const Config::Location& location = locations[i];

            // Check if location path is safely accessible
            if (location.path.empty()) {
                continue;
            }

            const std::string& locationPath = location.path;

            // Check if path starts with locationPath
            if (path.find(locationPath) == 0) {
                // Verify this is a valid directory match
                bool isValidMatch = false;

                if (locationPath == "/") {
                    // Root location matches everything
                    isValidMatch = true;
                } else if (path.length() == locationPath.length()) {
                    // Exact match (e.g., /upload == /upload)
                    isValidMatch = true;
                } else if (path[locationPath.length()] == '/') {
                    // Directory match (e.g., /upload/file matches /upload)
                    isValidMatch = true;
                }

                if (isValidMatch && locationPath.length() > bestMatchLength) {
                    bestMatch = &location;
                    bestMatchLength = locationPath.length();
                }
            }
        }

        return bestMatch;
    } catch (...) {
        // If anything goes wrong, return NULL safely
        return NULL;
    }
}

bool HttpServer::isMethodAllowed(const std::string& method, const Config::Location& location) {
    const std::set<std::string>& allowedMethods = location.allowMethods;

    if (allowedMethods.empty()) {
        return true;
    }

    for (std::set<std::string>::const_iterator it = allowedMethods.begin();
         it != allowedMethods.end(); ++it) {
        if (*it == method) {
            return true;
        }
    }

    return false;
}

bool HttpServer::isPathSafe(const std::string& path) {
    if (path.find("..") != std::string::npos) {
        return false;
    }

    if (path.empty() || path[0] != '/') {
        return false;
    }

    return true;
}

bool HttpServer::isCGIFile(const std::string& filePath) {
    size_t dotPos = filePath.find_last_of('.');
    if (dotPos == std::string::npos) {
        return false;
    }

    std::string extension = filePath.substr(dotPos);
    return (extension == ".php" || extension == ".py" || extension == ".cgi" || extension == ".pl" ||
            extension == ".bla");
}

std::string HttpServer::resolvePath(const std::string&      requestPath,
                                    const Config::Location& location) const {
    std::string basePath = location.root.empty() ? m_DocumentRoot : location.root;

    if (requestPath == "/") {
        return basePath;
    }

    return joinPath(basePath, requestPath);
}

std::string HttpServer::joinPath(const std::string& baseDir, const std::string& fileName) {
    if (baseDir.empty()) {
        return fileName;
    }
    if (fileName.empty()) {
        return baseDir;
    }

    std::string result = baseDir;
    if (result[result.length() - 1] != '/' && fileName[0] != '/') {
        result += '/';
    } else if (result[result.length() - 1] == '/' && fileName[0] == '/') {
        result = result.substr(0, result.length() - 1);
    }

    result += fileName;
    return result;
}

/* @------------------------------------------------------------------------@ */
/* |                             Testing Methods                            | */
/* @------------------------------------------------------------------------@ */

HttpResponse HttpServer::testGenerateDirectoryListing(const std::string&    dirPath,
                                                      const std::string&    requestPath,
                                                      const Config::Server& server) {
    return generateDirectoryListing(dirPath, requestPath, server);
}

HttpResponse HttpServer::testServeStaticFile(const std::string&    filePath,
                                             const Config::Server& server) {
    return serveStaticFile(filePath, server);
}

HttpResponse HttpServer::testCreateErrorResponse(int statusCode, const Config::Server& server) {
    return createErrorResponse(statusCode, server);
}

bool HttpServer::testIsPathSafe(const std::string& path) { return isPathSafe(path); }

std::string HttpServer::testResolvePath(const std::string&      requestPath,
                                        const Config::Location& location) const {
    return resolvePath(requestPath, location);
}

std::string HttpServer::testJoinPath(const std::string& base, const std::string& path) {
    return joinPath(base, path);
}

/* @------------------------------------------------------------------------@ */
/* |                              CGI Handler                               | */
/* @------------------------------------------------------------------------@ */

HttpResponse HttpServer::handleCGI(const HttpRequest& request, const Config::Server& server,
                                   const std::string& filePath) {
    (void)server;  // Suppress unused parameter warning for now

    // Static counter for unique temp file names (time() is not allowed)
    static int tempFileCounter = 0;
    tempFileCounter++;

    m_Logger.info() << "CGI request to " << filePath;

    // Determine CGI interpreter based on file extension
    std::string interpreter;
    if (filePath.find(".php") != std::string::npos) {
        interpreter = "/usr/bin/php-cgi";
    } else if (filePath.find(".py") != std::string::npos) {
        interpreter = "/usr/bin/python3";
    } else if (filePath.find(".pl") != std::string::npos) {
        interpreter = "/usr/bin/perl";
    } else if (filePath.find(".bla") != std::string::npos) {
        interpreter = "./cgi_test";
    } else {
        // For .cgi files, execute directly
        interpreter = "";
    }

    // Prepare request body info BEFORE fork()
    // For large uploads, we DON'T read into memory (would be 1GB+!)
    // Instead, we'll redirect stdin from the temp file in the child process
    std::string requestBody;
    std::string tempFilePath;
    size_t      contentLength = 0;
    bool        hasLargeUpload = false;

    if (request.getMethod() == "POST") {
        if (request.hasLargeUpload()) {
            // Large upload - get temp file path but DON'T read into memory
            hasLargeUpload = true;
            tempFilePath = request.getTempFilePath();

            // Get file size for CONTENT_LENGTH
            struct stat fileStat;
            if (stat(tempFilePath.c_str(), &fileStat) == 0) {
                contentLength = fileStat.st_size;
            }

            m_Logger.info() << "CGI large upload: " << contentLength
                           << " bytes from temp file: " << tempFilePath;
        } else {
            // Small/medium upload - read into memory
            requestBody = request.getBody();
            contentLength = requestBody.length();

            // If body is large (>=1MB) but came via chunked encoding (no streaming):
            // Create temp file on-the-fly to avoid pipe deadlock
            if (contentLength >= LARGE_FILE_THRESHOLD) {
                m_Logger.info() << "CGI large body (" << contentLength
                               << " bytes) without temp file - creating temp file for pipe safety";

                // Generate unique temp file name
                std::ostringstream tempNameStream;
                tempNameStream << "./html/.cgi_temp_" << getpid() << "_" << tempFileCounter;
                tempFilePath = tempNameStream.str();

                // Write body to temp file
                int tempFd = open(tempFilePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
                if (tempFd == -1) {
                    m_Logger.error() << "Failed to create temp file for large CGI body: "
                                     << tempFilePath;
                    return createErrorResponse(HTTP_INTERNAL_ERROR, server);
                }

                ssize_t written = write(tempFd, requestBody.c_str(), requestBody.length());
                close(tempFd);

                if (written != static_cast<ssize_t>(requestBody.length())) {
                    m_Logger.error() << "Failed to write body to temp file";
                    unlink(tempFilePath.c_str());
                    return createErrorResponse(HTTP_INTERNAL_ERROR, server);
                }

                hasLargeUpload = true;
                requestBody.clear();  // Free memory since we now have it on disk

                m_Logger.info() << "CGI large body written to temp file: " << tempFilePath;
            }
        }
    }

    // Calculate PATH_INFO BEFORE fork() to avoid using object methods in child
    std::string path = request.getPath();
    std::string queryString;
    size_t      queryPos = path.find('?');
    if (queryPos != std::string::npos) {
        queryString = path.substr(queryPos + 1);
        path = path.substr(0, queryPos);
    }

    // PATH_INFO and SCRIPT_NAME for CGI
    // ubuntu_cgi_tester expects SCRIPT_NAME to be empty and PATH_INFO to contain the full request path
    // For /directory/youpi.bla -> SCRIPT_NAME="" and PATH_INFO="/directory/youpi.bla"
    std::string pathInfo = path;           // Full request path
    std::string scriptName = "";           // Empty for ubuntu_cgi_tester compatibility

    m_Logger.info() << "CGI PATH_INFO='" << pathInfo << "' SCRIPT_NAME='" << scriptName << "'";

    // Create pipes for CGI communication
    int stdinPipe[2];
    int stdoutPipe[2];

    if (pipe(stdinPipe) == -1 || pipe(stdoutPipe) == -1) {
        m_Logger.error() << "Failed to create pipes for CGI";
        return createErrorResponse(HTTP_INTERNAL_ERROR, server);
    }

    // Fork child process for CGI execution
    pid_t pid = fork();
    if (pid == -1) {
        m_Logger.error() << "Failed to fork for CGI execution";
        close(stdinPipe[0]);
        close(stdinPipe[1]);
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);
        return createErrorResponse(HTTP_INTERNAL_ERROR, server);
    }

    if (pid == 0) {
        // Child process - execute CGI script

        // Set up stdin: either from temp file (large upload) or from pipe (small)
        if (hasLargeUpload && !tempFilePath.empty()) {
            // Large upload: redirect stdin from temp file (disk files are exempt from poll())
            int tempFd = open(tempFilePath.c_str(), O_RDONLY);
            if (tempFd == -1) {
                _exit(1);  // Failed to open temp file
            }
            dup2(tempFd, STDIN_FILENO);
            close(tempFd);
            // Close stdin pipe (not needed)
            close(stdinPipe[0]);
            close(stdinPipe[1]);
        } else {
            // Small upload: use pipe as before
            dup2(stdinPipe[0], STDIN_FILENO);
            close(stdinPipe[1]);
            close(stdinPipe[0]);
        }

        // Set up stdout/stderr
        dup2(stdoutPipe[1], STDOUT_FILENO);
        dup2(stdoutPipe[1], STDERR_FILENO);
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);

        // Set environment variables according to CGI standard
        // Use pre-calculated values from before fork()
        setenv("REQUEST_METHOD", request.getMethod().c_str(), 1);
        setenv("QUERY_STRING", queryString.c_str(), 1);
        setenv("PATH_INFO", pathInfo.c_str(), 1);
        setenv("SCRIPT_NAME", scriptName.c_str(), 1);
        setenv("SERVER_PROTOCOL", request.getVersion().c_str(), 1);

        // Content-related variables
        std::ostringstream contentLengthStream;
        contentLengthStream << contentLength;
        std::string contentLengthStr = contentLengthStream.str();
        setenv("CONTENT_LENGTH", contentLengthStr.c_str(), 1);
        setenv("CONTENT_TYPE", request.getHeader("Content-Type").c_str(), 1);
        setenv("SERVER_SOFTWARE", "webserv/1.0", 1);
        setenv("SERVER_NAME", "localhost", 1);

        // Get actual server port
        std::ostringstream portStream;
        if (!server.listens.empty()) {
            portStream << server.listens[0].second;
        } else {
            portStream << "8080";  // fallback
        }
        setenv("SERVER_PORT", portStream.str().c_str(), 1);

        // HTTP headers as environment variables
        setenv("HTTP_HOST", request.getHeader("Host").c_str(), 1);
        setenv("HTTP_USER_AGENT", request.getHeader("User-Agent").c_str(), 1);

        // Execute CGI script
        if (interpreter.empty()) {
            // Execute script directly (for .cgi files)
            char* argv[] = {const_cast<char*>(filePath.c_str()), NULL};
            execve(filePath.c_str(), argv, environ);
        } else {
            // Execute with interpreter (for .php, .py, .pl, .bla files)
            char* argv[] = {const_cast<char*>(interpreter.c_str()),
                            const_cast<char*>(filePath.c_str()), NULL};
            execve(interpreter.c_str(), argv, environ);
        }

        // If execve fails
        _exit(1);
    } else {
        // Parent process - read CGI output

        // Close child's pipe ends
        close(stdinPipe[0]);
        close(stdoutPipe[1]);

        // Send request body to CGI if needed (for POST with small body)
        // For large uploads, child reads directly from temp file (stdin already redirected)
        if (!hasLargeUpload && !requestBody.empty()) {
            write(stdinPipe[1], requestBody.c_str(), requestBody.length());
        }
        close(stdinPipe[1]);

        // Read CGI output - use temp file for large outputs to avoid memory issues
        std::string cgiOutput;
        std::string cgiOutputFile;
        int         outputFd = -1;
        size_t      totalOutputSize = 0;
        char        buffer[CGI_BUFFER_SIZE];
        ssize_t     bytesRead;

        while ((bytesRead = read(stdoutPipe[0], buffer, sizeof(buffer))) > 0) {
            totalOutputSize += bytesRead;

            // If output becomes large (>1MB) and we haven't created temp file yet, create it
            if (totalOutputSize > LARGE_FILE_THRESHOLD && outputFd == -1) {
                // Switch to temp file mode
                std::ostringstream tempNameStream;
                tempNameStream << "./html/.cgi_output_" << getpid() << "_" << tempFileCounter;
                cgiOutputFile = tempNameStream.str();

                outputFd = open(cgiOutputFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
                if (outputFd == -1) {
                    m_Logger.error() << "Failed to create temp file for CGI output: " << cgiOutputFile;
                    // Continue with in-memory storage
                } else {
                    m_Logger.info() << "CGI output large (" << totalOutputSize
                                   << " bytes) - writing to temp file: " << cgiOutputFile;
                    // Write accumulated data to file
                    if (!cgiOutput.empty()) {
                        write(outputFd, cgiOutput.c_str(), cgiOutput.length());
                        cgiOutput.clear();  // Free memory
                    }
                }
            }

            // Write to temp file or accumulate in memory
            if (outputFd != -1) {
                write(outputFd, buffer, bytesRead);
            } else {
                cgiOutput.append(buffer, bytesRead);
            }
        }

        if (outputFd != -1) {
            close(outputFd);
        }
        close(stdoutPipe[0]);

        // Wait for child to complete
        int status;
        waitpid(pid, &status, 0);

        // Cleanup temp file if we created one on-the-fly (for chunked large bodies)
        if (!tempFilePath.empty() && tempFilePath.find(".cgi_temp_") != std::string::npos) {
            if (unlink(tempFilePath.c_str()) == 0) {
                m_Logger.info() << "Cleaned up CGI temp file: " << tempFilePath;
            } else {
                m_Logger.warn() << "Failed to cleanup CGI temp file: " << tempFilePath;
            }
        }

        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            // CGI executed successfully
            m_Logger.info() << "CGI executed successfully, output size: " << totalOutputSize;

            // Parse CGI output to separate headers from body
            std::string cgiHeaders;
            std::string cgiBody;
            std::string fullOutput;

            // Get the full CGI output (from file or memory)
            if (!cgiOutputFile.empty()) {
                // Read from temp file
                std::ifstream file(cgiOutputFile.c_str(), std::ios::binary);
                if (!file.good()) {
                    m_Logger.error() << "Failed to read CGI output file: " << cgiOutputFile;
                    unlink(cgiOutputFile.c_str());
                    return createErrorResponse(HTTP_INTERNAL_ERROR, server);
                }
                std::ostringstream buffer;
                buffer << file.rdbuf();
                fullOutput = buffer.str();
                file.close();
                unlink(cgiOutputFile.c_str());
                m_Logger.info() << "Read and cleaned up CGI output temp file: " << cgiOutputFile;
            } else {
                fullOutput = cgiOutput;
            }

            // Parse CGI headers (format: "Header: value\r\n...\r\n\r\nbody")
            size_t headerEnd = fullOutput.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                cgiHeaders = fullOutput.substr(0, headerEnd);
                cgiBody = fullOutput.substr(headerEnd + 4);  // Skip \r\n\r\n
                m_Logger.info() << "Parsed CGI output: " << cgiHeaders.length()
                               << " bytes headers, " << cgiBody.length() << " bytes body";
            } else {
                // No CGI headers found, treat entire output as body
                cgiBody = fullOutput;
                m_Logger.warn() << "No CGI headers found in output, using entire output as body";
            }

            // Create HTTP response with parsed CGI headers
            HttpResponse response(HTTP_OK, m_Logger);

            // Parse and apply CGI headers
            if (!cgiHeaders.empty()) {
                std::istringstream headerStream(cgiHeaders);
                std::string line;
                while (std::getline(headerStream, line)) {
                    // Remove \r if present
                    if (!line.empty() && line[line.length() - 1] == '\r') {
                        line = line.substr(0, line.length() - 1);
                    }

                    size_t colonPos = line.find(':');
                    if (colonPos != std::string::npos) {
                        std::string headerName = line.substr(0, colonPos);
                        std::string headerValue = line.substr(colonPos + 1);

                        // Trim leading/trailing whitespace from value
                        size_t start = headerValue.find_first_not_of(" \t");
                        if (start != std::string::npos) {
                            headerValue = headerValue.substr(start);
                        }
                        size_t end = headerValue.find_last_not_of(" \t\r\n");
                        if (end != std::string::npos) {
                            headerValue = headerValue.substr(0, end + 1);
                        }

                        // Apply header (skip Status as it's handled separately)
                        if (headerName != "Status") {
                            response.setHeader(headerName, headerValue);
                            m_Logger.info() << "CGI header: " << headerName << ": " << headerValue;
                        }
                    }
                }
            } else {
                // Default Content-Type if no CGI headers
                response.setHeader("Content-Type", "text/html");
            }

            // Set the body (without CGI headers)
            response.setBody(cgiBody);
            return response;
        }

        // CGI execution failed
        // Clean up output temp file if created
        if (!cgiOutputFile.empty()) {
            unlink(cgiOutputFile.c_str());
        }
        m_Logger.error() << "CGI execution failed with status: " << WEXITSTATUS(status);
        return createErrorResponse(HTTP_INTERNAL_ERROR, server);
    }
}

/* @------------------------------------------------------------------------@ */
/* |                          Error Response Method                         | */
/* @------------------------------------------------------------------------@ */

HttpResponse HttpServer::createErrorResponse(int statusCode, const Config::Server& server) {
    // Try to serve custom error page if configured
    std::map<int, std::string>::const_iterator it = server.errorPages.find(statusCode);
    if (it != server.errorPages.end()) {
        const std::string& errorPagePath = it->second;

        // Check if custom error page exists and is accessible
        struct stat errorStat;
        if (stat(errorPagePath.c_str(), &errorStat) == 0 && S_ISREG(errorStat.st_mode) &&
            access(errorPagePath.c_str(), R_OK) == 0) {
            HttpResponse response(statusCode, m_Logger);
            response.setBodyFromFile(errorPagePath);

            // If custom error page loaded successfully, use it
            if (response.getContentLength() > 0) {
                return response;
            }
        }

        m_Logger.warn() << "Custom error page not accessible: " << errorPagePath << " for status "
                        << statusCode;
    }

    // Fallback to default error responses
    switch (statusCode) {
        case HTTP_BAD_REQUEST:
            return HttpResponse::createBadRequest();
        case HTTP_FORBIDDEN:
            return HttpResponse::createForbidden();
        case HTTP_NOT_FOUND:
            return HttpResponse::createNotFound();
        case HTTP_METHOD_NOT_ALLOWED:
            return HttpResponse::createMethodNotAllowed();
        case HTTP_PAYLOAD_TOO_LARGE: {
            HttpResponse response = HttpResponse::createInternalError();
            response.setStatus(HTTP_PAYLOAD_TOO_LARGE, "Payload Too Large");
            return response;
        }
        case HTTP_INTERNAL_ERROR:
        default:
            return HttpResponse::createInternalError();
    }
}
