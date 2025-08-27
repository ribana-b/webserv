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
#include <cerrno>     // For errno, EXDEV
#include <cstring>    // For strerror
#include <ctime>      // For time()
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
    if (!server) {
        m_Logger.error() << "No server configuration found for port " << serverPort;
        return HttpResponse::createInternalError("Server configuration error");
    }

    const std::string& method = request.getMethod();

    if (method == "GET") {
        return handleGET(request, *server);
    } else if (method == "POST") {
        return handlePOST(request, *server);
    } else if (method == "DELETE") {
        return handleDELETE(request, *server);
    } else if (method == "HEAD") {
        return handleHEAD(request, *server);
    } else {
        m_Logger.warn() << "Method not allowed: " << method;
        return createErrorResponse(405, *server);
    }
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
        return createErrorResponse(403, server);
    }

    // Find matching location for this request
    const Config::Location* location = findMatchingLocation(server, requestPath);

    // Check if method is allowed for this location
    if (location && !isMethodAllowed("GET", *location)) {
        m_Logger.warn() << "GET method not allowed for path: " << requestPath;
        return createErrorResponse(405, server);
    }

    // Determine document root and index file
    std::string documentRoot;
    std::string indexFile = "index.html";

    if (location && !location->root.empty()) {
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
        return createErrorResponse(404, server);
    }

    if (S_ISREG(fileStat.st_mode)) {
        // Check if it's a CGI file
        if (isCGIFile(filePath)) {
            return handleCGI(request, server, filePath);
        } else {
            return serveStaticFile(filePath, server);
        }
    } else if (S_ISDIR(fileStat.st_mode)) {
        return generateDirectoryListing(filePath, requestPath, server);
    } else {
        return createErrorResponse(403, server);
    }
}

HttpResponse HttpServer::handlePOST(const HttpRequest& request, const Config::Server& server) {
    const std::string& requestPath = request.getPath();

    m_Logger.info() << "POST request to " << requestPath << " (body: " << request.getBody().length()
                    << " bytes)";

    // Find matching location for this request
    const Config::Location* location = findMatchingLocation(server, requestPath);

    // Check if method is allowed for this location
    if (location && !isMethodAllowed("POST", *location)) {
        m_Logger.warn() << "POST method not allowed for path: " << requestPath;
        return createErrorResponse(405, server);
    }

    // Check client body size limit
    if (location && location->clientMaxBodySize > 0 &&
        request.getBody().length() > location->clientMaxBodySize) {
        m_Logger.warn() << "Request body too large: " << request.getBody().length() << " > "
                        << location->clientMaxBodySize;
        return createErrorResponse(413, server);
    }

    // Check if this is a CGI script request
    std::string cleanPath = requestPath;
    size_t      queryPos = cleanPath.find('?');
    if (queryPos != std::string::npos) {
        cleanPath = cleanPath.substr(0, queryPos);
    }

    // Determine document root
    std::string documentRoot;
    if (location && !location->root.empty()) {
        documentRoot = location->root;
    } else {
        documentRoot = server.root;
    }
    if (documentRoot.empty()) {
        documentRoot = "./html";
    }

    // Construct file path for CGI check
    std::string filePath = documentRoot + cleanPath;

    // If it's a CGI file, handle it as CGI instead of upload
    if (isCGIFile(filePath)) {
        struct stat fileStat;
        if (stat(filePath.c_str(), &fileStat) == 0 && S_ISREG(fileStat.st_mode)) {
            return handleCGI(request, server, filePath);
        }
    }

    // File upload implementation with support for large files
    if (requestPath == "/upload") {
        std::string body;
        bool        isLargeUpload = false;

        // Check if this is a large upload using temp file
        if (request.hasLargeUpload()) {
            isLargeUpload = true;
            m_Logger.info() << "Processing large upload from temp file: "
                            << request.getTempFilePath();
            // For large uploads, we'll move the temp file instead of reading into memory
        } else {
            body = request.getBody();
            if (body.empty()) {
                m_Logger.warn() << "Empty upload request body";
                return createErrorResponse(400, server);
            }
        }

        // Generate final filename
        std::ostringstream oss;
        oss << "./html/uploaded_" << time(NULL);
        if (isLargeUpload) {
            oss << "_large.bin";  // Use binary extension for large files
        } else {
            oss << ".txt";
        }
        std::string filename = oss.str();

        bool        success = false;
        std::size_t fileSize = 0;

        if (isLargeUpload) {
            // Move temp file to final location

            // Check if temp file exists before rename
            std::ifstream tempCheck(request.getTempFilePath().c_str());
            if (!tempCheck.good()) {
                m_Logger.error() << "Temp file does not exist or is not readable: "
                                 << request.getTempFilePath();
            } else {
                tempCheck.close();
            }

            int renameResult = rename(request.getTempFilePath().c_str(), filename.c_str());
            if (renameResult == 0) {
                success = true;
                // Get file size for reporting
                std::ifstream file(filename.c_str(), std::ios::binary | std::ios::ate);
                if (file.is_open()) {
                    fileSize = static_cast<std::size_t>(file.tellg());
                    file.close();
                }
                m_Logger.info() << "Large file moved successfully from "
                                << request.getTempFilePath() << " to " << filename << " ("
                                << fileSize << " bytes)";
            } else if (errno == EXDEV) {
                // Cross-device link error: copy and delete instead of rename

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
                            if (unlink(request.getTempFilePath().c_str()) == 0) {
                                success = true;
                                m_Logger.info() << "Large file copied successfully from "
                                                << request.getTempFilePath() << " to " << filename
                                                << " (" << fileSize << " bytes)";
                            } else {
                                m_Logger.warn() << "File copied but failed to delete temp file: "
                                                << request.getTempFilePath();
                                success = true;  // Still consider it successful
                            }
                        } else {
                            m_Logger.error() << "Failed to verify copied file: " << filename;
                        }
                    } else {
                        source.close();
                        m_Logger.error()
                            << "Failed to open destination file for writing: " << filename;
                    }
                } else {
                    m_Logger.error()
                        << "Failed to open temp file for reading: " << request.getTempFilePath();
                }
            } else {
                m_Logger.error() << "Failed to move large upload from " << request.getTempFilePath()
                                 << " to " << filename << " (errno: " << errno << " - "
                                 << strerror(errno) << ")";
            }
        } else {
            // Small file: write normally
            std::ofstream outFile(filename.c_str(), std::ios::binary);
            if (outFile.is_open()) {
                outFile << body;
                outFile.close();
                success = true;
                fileSize = body.length();
                m_Logger.info() << "Small file uploaded successfully: " << filename << " ("
                                << fileSize << " bytes)";
            }
        }

        if (success) {
            std::ostringstream responseBody;
            responseBody << "<h1>Upload Successful!</h1><p>File saved as: " << filename
                         << "</p><p>Size: " << fileSize << " bytes</p>"
                         << "<p>Type: "
                         << (isLargeUpload ? "Large file (streamed to disk)"
                                           : "Small file (in memory)")
                         << "</p>";

            HttpResponse response(200, m_Logger);
            response.setHeader("Content-Type", "text/html");
            response.setBody(responseBody.str());
            return response;
        } else {
            m_Logger.error() << "Failed to save uploaded file: " << filename;
            return createErrorResponse(500, server);
        }
    }

    // Default POST response
    HttpResponse response(200, m_Logger);
    response.setHeader("Content-Type", "text/plain");
    response.setBody("POST request processed successfully");
    return response;
}

HttpResponse HttpServer::handleDELETE(const HttpRequest& request, const Config::Server& server) {
    const std::string& requestPath = request.getPath();

    m_Logger.info() << "DELETE request to " << requestPath;

    if (!isPathSafe(requestPath)) {
        m_Logger.warn() << "Unsafe path detected in DELETE: " << requestPath;
        return createErrorResponse(403, server);
    }

    // Find matching location for this request
    const Config::Location* location = findMatchingLocation(server, requestPath);

    // Check if method is allowed for this location
    if (location && !isMethodAllowed("DELETE", *location)) {
        m_Logger.warn() << "DELETE method not allowed for path: " << requestPath;
        return createErrorResponse(405, server);
    }

    // Determine document root
    std::string documentRoot;
    if (location && !location->root.empty()) {
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
        return createErrorResponse(404, server);
    }

    if (S_ISREG(fileStat.st_mode)) {
        if (unlink(filePath.c_str()) == 0) {
            m_Logger.info() << "File deleted successfully: " << filePath;

            HttpResponse response(200, m_Logger);
            response.setHeader("Content-Type", "text/html");
            response.setBody("<h1>Delete Successful!</h1><p>File deleted: " + requestPath + "</p>");
            return response;
        } else {
            m_Logger.error() << "Failed to delete file: " << filePath;
            return createErrorResponse(500, server);
        }
    } else {
        m_Logger.warn() << "Cannot delete non-regular file: " << filePath;
        return createErrorResponse(403, server);
    }
}

HttpResponse HttpServer::handleHEAD(const HttpRequest& request, const Config::Server& server) {
    const std::string& requestPath = request.getPath();

    if (!isPathSafe(requestPath)) {
        m_Logger.warn() << "Unsafe path detected: " << requestPath;
        return createErrorResponse(403, server);
    }

    // Find matching location for this request
    const Config::Location* location = findMatchingLocation(server, requestPath);

    // Check if method is allowed for this location
    if (location && !isMethodAllowed("HEAD", *location)) {
        m_Logger.warn() << "HEAD method not allowed for path: " << requestPath;
        return createErrorResponse(405, server);
    }

    // Determine document root and index file
    std::string documentRoot;
    std::string indexFile = "index.html";

    if (location && !location->root.empty()) {
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

    if (documentRoot.empty() || documentRoot.length() > 500) {
        m_Logger.warn() << "HEAD: Invalid server.root, using fallback";
        documentRoot = "./html";
    }

    // Get index file from server config
    if (!server.index.empty()) {
        indexFile = server.index[0];
        if (indexFile.length() > 100 || indexFile.find("..") != std::string::npos) {
            m_Logger.warn() << "HEAD: Invalid index file name, using default";
            indexFile = "index.html";
        }
    }

    // Validate path lengths before concatenation
    if (documentRoot.length() + requestPath.length() > 800) {
        m_Logger.error() << "HEAD: Combined path would be too long";
        return createErrorResponse(414, server);
    }

    // Safe path construction
    std::string filePath;
    try {
        if (requestPath == "/") {
            filePath = documentRoot + "/" + indexFile;
        } else {
            filePath = documentRoot + requestPath;
        }

        if (filePath.length() > 1000) {
            m_Logger.error() << "HEAD: Final filePath too long: " << filePath.length();
            return createErrorResponse(414, server);
        }

    } catch (const std::exception& e) {
        m_Logger.error() << "HEAD: String concatenation failed: " << e.what();
        return createErrorResponse(500, server);
    }

    struct stat fileStat;
    if (stat(filePath.c_str(), &fileStat) != 0) {
        return createErrorResponse(404, server);
    }

    if (S_ISREG(fileStat.st_mode)) {
        HttpResponse response(200, m_Logger);

        // Simple MIME type detection
        std::string contentType = "application/octet-stream";
        std::size_t dotPos = filePath.find_last_of('.');
        if (dotPos != std::string::npos) {
            std::string extension = filePath.substr(dotPos);
            if (extension == ".html" || extension == ".htm")
                contentType = "text/html; charset=utf-8";
            else if (extension == ".css")
                contentType = "text/css";
            else if (extension == ".js")
                contentType = "application/javascript";
            else if (extension == ".txt")
                contentType = "text/plain; charset=utf-8";
            else if (extension == ".jpg" || extension == ".jpeg")
                contentType = "image/jpeg";
            else if (extension == ".png")
                contentType = "image/png";
        }

        response.setHeader("Content-Type", contentType);

        std::ostringstream oss;
        oss << fileStat.st_size;
        response.setHeader("Content-Length", oss.str());

        return response;
    } else if (S_ISDIR(fileStat.st_mode)) {
        HttpResponse response(200, m_Logger);
        response.setHeader("Content-Type", "text/html");
        return response;
    } else {
        return createErrorResponse(403, server);
    }
}

HttpResponse HttpServer::serveStaticFile(const std::string&    filePath,
                                         const Config::Server& server) {
    // Security check: Detect and reject symbolic links
    struct stat linkStat;
    if (lstat(filePath.c_str(), &linkStat) != 0) {
        return createErrorResponse(404, server);
    }

    if (S_ISLNK(linkStat.st_mode)) {
        m_Logger.warn() << "Symbolic link rejected for security reasons: " << filePath;
        return createErrorResponse(403, server);
    }

    // Check if file exists and get stats (using stat for regular files)
    struct stat fileStat;
    if (stat(filePath.c_str(), &fileStat) != 0) {
        return createErrorResponse(404, server);
    }

    // Verify it's a regular file
    if (!S_ISREG(fileStat.st_mode)) {
        m_Logger.warn() << "Not a regular file: " << filePath;
        return createErrorResponse(403, server);
    }

    // Check read permissions
    if (access(filePath.c_str(), R_OK) != 0) {
        m_Logger.warn() << "No read permission for file: " << filePath;
        return createErrorResponse(403, server);
    }

    // Check file size (basic limit of 100MB for safety)
    const std::size_t MAX_FILE_SIZE = 100 * 1024 * 1024;
    if (static_cast<std::size_t>(fileStat.st_size) > MAX_FILE_SIZE) {
        m_Logger.warn() << "File too large: " << filePath << " (" << fileStat.st_size << " bytes)";
        return createErrorResponse(413, server);
    }

    HttpResponse response(200, m_Logger);
    response.setBodyFromFile(filePath);

    // Double-check that file loading succeeded
    if (response.getStatusCode() != 200) {
        m_Logger.error() << "Failed to load file content: " << filePath;
        return createErrorResponse(500, server);
    }

    m_Logger.info() << "Served file: " << filePath << " (" << response.getContentLength()
                    << " bytes)";
    return response;
}

HttpResponse HttpServer::generateDirectoryListing(const std::string&    dirPath,
                                                  const std::string&    requestPath,
                                                  const Config::Server& server) {
    (void)server;

    HttpResponse response(200, m_Logger);
    response.setHeader("Content-Type", "text/html; charset=utf-8");

    DIR* dir = opendir(dirPath.c_str());
    if (!dir) {
        m_Logger.warn() << "Cannot open directory: " << dirPath;
        return createErrorResponse(403, server);
    }

    // Collect directory entries
    std::vector<std::string> directories;
    std::vector<std::string> files;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        std::string name = entry->d_name;

        // Skip . and .. but allow hidden files starting with .
        if (name == "." || name == "..") {
            continue;
        }

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

    // Generate HTML
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

    // Add parent directory link if not root
    if (requestPath != "/") {
        std::string parentPath = requestPath;
        if (parentPath.length() > 1 && parentPath[parentPath.length() - 1] == '/') {
            parentPath = parentPath.substr(0, parentPath.length() - 1);
        }
        std::size_t lastSlash = parentPath.rfind('/');
        if (lastSlash != std::string::npos) {
            parentPath = parentPath.substr(0, lastSlash);
            if (parentPath.empty()) parentPath = "/";
        }
        html << "<a href=\"" << parentPath << "\" class=\"directory\">[Parent Directory]</a>\n";
    }

    // Add directories
    for (std::vector<std::string>::const_iterator it = directories.begin(); it != directories.end();
         ++it) {
        std::string linkPath = requestPath;
        if (linkPath[linkPath.length() - 1] != '/') linkPath += "/";
        linkPath += *it;

        std::string fullPath = joinPath(dirPath, *it);
        struct stat entryStat;
        std::string sizeInfo = "";
        if (stat(fullPath.c_str(), &entryStat) == 0) {
            sizeInfo = "<span class=\"size\">[DIR]</span>";
        }

        html << "<a href=\"" << linkPath << "/\" class=\"directory\">" << *it << "/" << sizeInfo
             << "</a>\n";
    }

    // Add files
    for (std::vector<std::string>::const_iterator it = files.begin(); it != files.end(); ++it) {
        std::string linkPath = requestPath;
        if (linkPath[linkPath.length() - 1] != '/') linkPath += "/";
        linkPath += *it;

        std::string fullPath = joinPath(dirPath, *it);
        struct stat entryStat;
        std::string sizeInfo = "";
        if (stat(fullPath.c_str(), &entryStat) == 0) {
            std::ostringstream size;
            if (entryStat.st_size < 1024) {
                size << entryStat.st_size << "B";
            } else if (entryStat.st_size < 1024 * 1024) {
                size << (entryStat.st_size / 1024) << "KB";
            } else {
                size << (entryStat.st_size / (1024 * 1024)) << "MB";
            }
            sizeInfo = "<span class=\"size\">" + size.str() + "</span>";
        }

        html << "<a href=\"" << linkPath << "\" class=\"file\">" << *it << sizeInfo << "</a>\n";
    }

    html << "</body></html>";

    response.setBody(html.str());
    m_Logger.info() << "Generated directory listing for: " << requestPath << " ("
                    << directories.size() << " dirs, " << files.size() << " files)";
    return response;
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
                                                         const std::string&    path) const {
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

bool HttpServer::isMethodAllowed(const std::string&      method,
                                 const Config::Location& location) const {
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

bool HttpServer::isPathSafe(const std::string& path) const {
    if (path.find("..") != std::string::npos) {
        return false;
    }

    if (path.empty() || path[0] != '/') {
        return false;
    }

    return true;
}

bool HttpServer::isCGIFile(const std::string& filePath) const {
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

std::string HttpServer::joinPath(const std::string& base, const std::string& path) const {
    if (base.empty()) return path;
    if (path.empty()) return base;

    std::string result = base;
    if (result[result.length() - 1] != '/' && path[0] != '/') {
        result += '/';
    } else if (result[result.length() - 1] == '/' && path[0] == '/') {
        result = result.substr(0, result.length() - 1);
    }

    result += path;
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

bool HttpServer::testIsPathSafe(const std::string& path) const { return isPathSafe(path); }

std::string HttpServer::testResolvePath(const std::string&      requestPath,
                                        const Config::Location& location) const {
    return resolvePath(requestPath, location);
}

std::string HttpServer::testJoinPath(const std::string& base, const std::string& path) const {
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
    int stdin_pipe[2];
    int stdout_pipe[2];

    if (pipe(stdin_pipe) == -1 || pipe(stdout_pipe) == -1) {
        m_Logger.error() << "Failed to create pipes for CGI";
        return createErrorResponse(500, server);
    }

    // Fork child process for CGI execution
    pid_t pid = fork();
    if (pid == -1) {
        m_Logger.error() << "Failed to fork for CGI execution";
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        return createErrorResponse(500, server);
    }

    if (pid == 0) {
        // Child process - execute CGI script

        // Set up pipes
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stdout_pipe[1], STDERR_FILENO);

        // Close unused pipe ends
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);

        // Set environment variables according to CGI standard
        setenv("REQUEST_METHOD", request.getMethod().c_str(), 1);

        // Extract query string from path (everything after '?')
        std::string path = request.getPath();
        std::string queryString = "";
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
            execl(filePath.c_str(), filePath.c_str(), (char*)NULL);
        } else {
            // Execute with interpreter (for .php, .py, .pl files)
            execlp(interpreter.c_str(), interpreter.c_str(), filePath.c_str(), (char*)NULL);
        }

        // If execl fails
        _exit(1);
    } else {
        // Parent process - read CGI output

        // Close child's pipe ends
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);

        // Send request body to CGI if needed (for POST)
        if (request.getMethod() == "POST" && !request.getBody().empty()) {
            write(stdin_pipe[1], request.getBody().c_str(), request.getBody().length());
        }
        close(stdin_pipe[1]);

        // Read CGI output
        std::string cgiOutput;
        char        buffer[8192];
        ssize_t     bytesRead;

        while ((bytesRead = read(stdout_pipe[0], buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytesRead] = '\0';
            cgiOutput += buffer;
        }
        close(stdout_pipe[0]);

        // Wait for child to complete
        int status;
        waitpid(pid, &status, 0);

        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            // CGI executed successfully
            m_Logger.info() << "CGI executed successfully, output size: " << cgiOutput.length();

            HttpResponse response(200, m_Logger);
            response.setHeader("Content-Type", "text/html");
            response.setBody(cgiOutput);
            return response;
        } else {
            // CGI execution failed
            m_Logger.error() << "CGI execution failed with status: " << WEXITSTATUS(status);
            return createErrorResponse(500, server);
        }
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
        case 400:
            return HttpResponse::createBadRequest();
        case 403: {
            HttpResponse response = HttpResponse::createBadRequest();
            response.setStatus(403, "Forbidden");
            return response;
        }
        case 404:
            return HttpResponse::createNotFound();
        case 405:
            return HttpResponse::createMethodNotAllowed();
        case 413: {
            HttpResponse response = HttpResponse::createInternalError();
            response.setStatus(413, "Payload Too Large");
            return response;
        }
        case 500:
            return HttpResponse::createInternalError();
        default:
            return HttpResponse::createInternalError();
    }
}
