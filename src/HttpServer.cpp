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

#include <dirent.h>      // For directory operations
#include <netinet/in.h>  // For ntohs
#include <sys/stat.h>    // For stat
#include <sys/wait.h>    // For waitpid
#include <unistd.h>      // For access, unlink, fork, exec, pipe

#include <algorithm>  // For std::sort
#include <cstring>    // For strerror
#include <fstream>    // For std::ofstream
#include <iostream>   // For std::cout
#include <set>        // For std::set
#include <sstream>    // For std::ostringstream
#include <vector>     // For std::vector

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

    // Construct file path
    std::string filePath;
    if (cleanPath == "/") {
        filePath = documentRoot + "/" + indexFile;
    } else {
        filePath = documentRoot + cleanPath;
    }

    struct stat fileStat;
    if (stat(filePath.c_str(), &fileStat) != 0) {
        return createErrorResponse(HTTP_NOT_FOUND, server);
    }

    if (S_ISREG(fileStat.st_mode)) {
        // Check if it's a CGI file
        if (isCGIFile(filePath)) {
            return handleCGI(request, server, filePath);
        }
        return serveStaticFile(filePath, server);
    }
    if (S_ISDIR(fileStat.st_mode)) {
        return generateDirectoryListing(filePath, requestPath, server);
    }
    return createErrorResponse(HTTP_FORBIDDEN, server);
}

HttpResponse HttpServer::handlePOST(const HttpRequest& request, const Config::Server& server) {
    const std::string& requestPath = request.getPath();

    m_Logger.info() << "POST request to " << requestPath << " (body: " << request.getBody().length()
                    << " bytes)";

    // Find matching location for this request
    const Config::Location* location = findMatchingLocation(server, requestPath);

    // Validate POST request parameters
    HttpResponse validationResponse = validatePOSTRequest(request, server, location, requestPath);
    if (validationResponse.getStatusCode() != HTTP_OK) {
        return validationResponse;
    }

    // Check if this is a CGI script request
    std::string cleanPath = requestPath;
    size_t      queryPos = cleanPath.find('?');
    if (queryPos != std::string::npos) {
        cleanPath = cleanPath.substr(0, queryPos);
    }

    // Determine document root and construct file path for CGI check
    std::string documentRoot = determinePOSTDocumentRoot(location, server);
    std::string filePath = documentRoot + cleanPath;

    // If it's a CGI file, handle it as CGI instead of upload
    if (isCGIFile(filePath)) {
        struct stat fileStat;
        if (stat(filePath.c_str(), &fileStat) == 0 && S_ISREG(fileStat.st_mode)) {
            return handleCGI(request, server, filePath);
        }
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
    // Check if temp file exists before rename
    std::ifstream tempCheck(request.getTempFilePath().c_str());
    if (!tempCheck.good()) {
        m_Logger.error() << "Temp file does not exist or is not readable: "
                         << request.getTempFilePath();
        return false;
    }
    tempCheck.close();

    int renameResult = rename(request.getTempFilePath().c_str(), filename.c_str());
    if (renameResult == 0) {
        // Get file size for reporting
        std::ifstream file(filename.c_str(), std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            fileSize = static_cast<std::size_t>(file.tellg());
            file.close();
        }
        m_Logger.info() << "Large file moved successfully from " << request.getTempFilePath()
                        << " to " << filename << " (" << fileSize << " bytes)";
        return true;
    }

    // Subject forbids errno checking - try copy and delete as fallback
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

// Helper method to process regular file uploads
bool HttpServer::processRegularFileUpload(const HttpRequest& request, const std::string& filename,
                                          std::size_t& fileSize) {
    const std::string& body = request.getBody();
    if (body.empty()) {
        m_Logger.warn() << "Empty upload request body";
        return false;
    }

    std::ofstream outFile(filename.c_str(), std::ios::binary);
    if (outFile.is_open()) {
        outFile << body;
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
    if (requestPath != "/upload") {
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

    // Generate final filename
    std::ostringstream  oss;
    static unsigned int uploadCounter = 0;
    oss << "./html/uploaded_" << ++uploadCounter;
    if (isLargeUpload) {
        oss << "_large.bin";  // Use binary extension for large files
    } else {
        oss << ".txt";
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

    // Construct file path
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

    // Construct and validate file path
    std::string filePath = constructHEADFilePath(documentRoot, requestPath, indexFile);
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
    // Security check: Detect and reject symbolic links
    struct stat linkStat;
    if (stat(filePath.c_str(), &linkStat) != 0) {
        return createErrorResponse(HTTP_NOT_FOUND, server);
    }

    if (S_ISLNK(linkStat.st_mode)) {
        m_Logger.warn() << "Symbolic link rejected for security reasons: " << filePath;
        return createErrorResponse(HTTP_FORBIDDEN, server);
    }

    // Check if file exists and get stats (using stat for regular files)
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

            if (path.find(locationPath) == 0 && locationPath.length() > bestMatchLength) {
                bestMatch = &location;
                bestMatchLength = locationPath.length();
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
    return (extension == ".php" || extension == ".py" || extension == ".cgi" || extension == ".pl");
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

    m_Logger.info() << "CGI request to " << filePath;

    // Determine CGI interpreter based on file extension
    std::string interpreter;
    if (filePath.find(".php") != std::string::npos) {
        interpreter = "php-cgi";
    } else if (filePath.find(".py") != std::string::npos) {
        interpreter = "python3";
    } else if (filePath.find(".pl") != std::string::npos) {
        interpreter = "perl";
    } else {
        // For .cgi files, execute directly
        interpreter = "";
    }

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

        // Set up pipes
        dup2(stdinPipe[0], STDIN_FILENO);
        dup2(stdoutPipe[1], STDOUT_FILENO);
        dup2(stdoutPipe[1], STDERR_FILENO);

        // Close unused pipe ends
        close(stdinPipe[1]);
        close(stdoutPipe[0]);
        close(stdinPipe[0]);
        close(stdoutPipe[1]);

        // Set environment variables according to CGI standard
        setenv("REQUEST_METHOD", request.getMethod().c_str(), 1);

        // Extract query string from path (everything after '?')
        std::string path = request.getPath();
        std::string queryString;
        size_t      queryPos = path.find('?');
        if (queryPos != std::string::npos) {
            queryString = path.substr(queryPos + 1);
            path = path.substr(0, queryPos);
        }
        setenv("QUERY_STRING", queryString.c_str(), 1);
        setenv("PATH_INFO", path.c_str(), 1);

        // Content-related variables
        std::ostringstream contentLengthStream;
        contentLengthStream << request.getBody().length();
        std::string contentLength = contentLengthStream.str();
        setenv("CONTENT_LENGTH", contentLength.c_str(), 1);
        setenv("CONTENT_TYPE", request.getHeader("Content-Type").c_str(), 1);

        // Server variables
        setenv("SCRIPT_NAME", path.c_str(), 1);
        setenv("SERVER_SOFTWARE", "webserv/1.0", 1);
        setenv("SERVER_NAME", "localhost", 1);
        setenv("SERVER_PORT", "8080", 1);

        // HTTP headers as environment variables
        setenv("HTTP_HOST", request.getHeader("Host").c_str(), 1);
        setenv("HTTP_USER_AGENT", request.getHeader("User-Agent").c_str(), 1);

        // Execute CGI script
        if (interpreter.empty()) {
            // Execute script directly (for .cgi files)
            char* argv[] = {const_cast<char*>(filePath.c_str()), NULL};
            char* envp[] = {NULL};  // Basic environment
            execve(filePath.c_str(), argv, envp);
        } else {
            // Execute with interpreter (for .php, .py, .pl files)
            char* argv[] = {const_cast<char*>(interpreter.c_str()),
                            const_cast<char*>(filePath.c_str()), NULL};
            char* envp[] = {NULL};  // Basic environment
            execve(interpreter.c_str(), argv, envp);
        }

        // If execve fails
        _exit(1);
    } else {
        // Parent process - read CGI output

        // Close child's pipe ends
        close(stdinPipe[0]);
        close(stdoutPipe[1]);

        // Send request body to CGI if needed (for POST)
        if (request.getMethod() == "POST" && !request.getBody().empty()) {
            write(stdinPipe[1], request.getBody().c_str(), request.getBody().length());
        }
        close(stdinPipe[1]);

        // Read CGI output
        std::string cgiOutput;
        char        buffer[CGI_BUFFER_SIZE];
        ssize_t     bytesRead;

        while ((bytesRead = read(stdoutPipe[0], buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytesRead] = '\0';
            cgiOutput += buffer;
        }
        close(stdoutPipe[0]);

        // Wait for child to complete
        int status;
        waitpid(pid, &status, 0);

        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            // CGI executed successfully
            m_Logger.info() << "CGI executed successfully, output size: " << cgiOutput.length();

            HttpResponse response(HTTP_OK, m_Logger);
            response.setHeader("Content-Type", "text/html");
            response.setBody(cgiOutput);
            return response;
        }

        // CGI execution failed
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
        case HTTP_FORBIDDEN: {
            HttpResponse response = HttpResponse::createBadRequest();
            response.setStatus(HTTP_FORBIDDEN, "Forbidden");
            return response;
        }
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
