/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HttpServer.hpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mancorte <mancorte@student.42malaga.com>   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/01/27 00:00:00 by mancorte          #+#    #+# Malaga      */
/*   Updated: 2025/01/27 00:00:00 by mancorte         ###   ########.com      */
/*                                                                            */
/* ************************************************************************** */

#ifndef HTTPSERVER_HPP
#define HTTPSERVER_HPP

/* @------------------------------------------------------------------------@ */
/* |                            Define Section                              | */
/* @------------------------------------------------------------------------@ */

#define HTTP_OK                 200
#define HTTP_CREATED            201
#define HTTP_NO_CONTENT         204
#define HTTP_MOVED_PERMANENTLY  301
#define HTTP_FOUND              302
#define HTTP_BAD_REQUEST        400
#define HTTP_UNAUTHORIZED       401
#define HTTP_FORBIDDEN          403
#define HTTP_NOT_FOUND          404
#define HTTP_METHOD_NOT_ALLOWED 405
#define HTTP_PAYLOAD_TOO_LARGE  413
#define HTTP_URI_TOO_LONG       414
#define HTTP_INTERNAL_ERROR     500
#define HTTP_NOT_IMPLEMENTED    501
#define HTTP_VERSION_NOT_SUPPORTED 505
#define CGI_BUFFER_SIZE         8192
#define BYTES_PER_KB            1024
#define BYTES_PER_MB            (1024 * 1024)
#define MIN_PATH_LENGTH         100
#define MAX_PATH_LENGTH         800
#define MAX_FILE_SIZE_MB        1000

/* @------------------------------------------------------------------------@ */
/* |                            Include Section                             | */
/* @------------------------------------------------------------------------@ */

#include <string>  // For std::string
#include <sys/types.h>  // For off_t

#include "Config.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "Logger.hpp"

/* @------------------------------------------------------------------------@ */
/* |                             Class Section                              | */
/* @------------------------------------------------------------------------@ */

class HttpServer {
public:
    HttpServer(const Config& config, const Logger& logger);
    ~HttpServer();
    HttpServer(const HttpServer& that);
    HttpServer& operator=(const HttpServer& that);

    HttpResponse processRequest(const HttpRequest& request, int serverPort);

    void setDocumentRoot(const std::string& root);
    void setDefaultIndex(const std::string& index);

    const std::string& getDocumentRoot() const;
    const std::string& getDefaultIndex() const;

    // Public methods for testing
    HttpResponse testGenerateDirectoryListing(const std::string&    dirPath,
                                              const std::string&    requestPath,
                                              const Config::Server& server);
    HttpResponse testServeStaticFile(const std::string& filePath, const Config::Server& server);
    HttpResponse testCreateErrorResponse(int statusCode, const Config::Server& server);
    static bool         testIsPathSafe(const std::string& path);
    std::string  testResolvePath(const std::string&      requestPath,
                                 const Config::Location& location) const;
    static std::string  testJoinPath(const std::string& base, const std::string& path);

private:
    const Config&  m_Config;
    mutable Logger m_Logger;
    std::string    m_DocumentRoot;
    std::string    m_DefaultIndex;

    HttpResponse handleGET(const HttpRequest& request, const Config::Server& server);
    HttpResponse handlePOST(const HttpRequest& request, const Config::Server& server);
    HttpResponse handleDELETE(const HttpRequest& request, const Config::Server& server);
    HttpResponse handleHEAD(const HttpRequest& request, const Config::Server& server);
    HttpResponse handleCGI(const HttpRequest& request, const Config::Server& server,
                           const std::string& filePath);

    HttpResponse serveStaticFile(const std::string& filePath, const Config::Server& server);
    HttpResponse generateDirectoryListing(const std::string&    dirPath,
                                          const std::string&    requestPath,
                                          const Config::Server& server);

    const Config::Server*   findMatchingServer(int port) const;
    static const Config::Location* findMatchingLocation(const Config::Server& server,
                                                  const std::string&    path);

    static bool        isMethodAllowed(const std::string& method, const Config::Location& location);
    static bool        isPathSafe(const std::string& path);
    static bool        isCGIFile(const std::string& filePath);
    std::string resolvePath(const std::string& requestPath, const Config::Location& location) const;
    static std::string joinPath(const std::string& baseDir, const std::string& fileName);

    HttpResponse createErrorResponse(int statusCode, const Config::Server& server);
    
    // Helper methods to reduce cognitive complexity
    HttpResponse validatePOSTRequest(const HttpRequest& request, const Config::Server& server, 
                                     const Config::Location* location, const std::string& requestPath);
    static std::string determinePOSTDocumentRoot(const Config::Location* location, const Config::Server& server);
    HttpResponse handleFileUpload(const HttpRequest& request, const Config::Server& server, 
                                  const std::string& requestPath);
    bool processLargeFileUpload(const HttpRequest& request, const std::string& filename, 
                                std::size_t& fileSize);
    bool processRegularFileUpload(const HttpRequest& request, const std::string& filename, 
                                  std::size_t& fileSize);
    
    // Helper methods for HEAD request processing
    HttpResponse validateHEADRequest(const HttpRequest& request, const Config::Server& server,
                                     const Config::Location* location, const std::string& requestPath);
    std::string determineHEADDocumentRoot(const Config::Location* location, const Config::Server& server,
                                          std::string& indexFile);
    std::string constructHEADFilePath(const std::string& documentRoot, const std::string& requestPath,
                                      const std::string& indexFile);
    static std::string determineContentTypeFromPath(const std::string& filePath);
    
    // Helper methods for directory listing generation
    bool collectDirectoryEntries(const std::string& dirPath, std::vector<std::string>& directories,
                                 std::vector<std::string>& files);
    static std::string generateDirectoryHTML(const std::string& requestPath, const std::string& dirPath,
                                      const std::vector<std::string>& directories,
                                      const std::vector<std::string>& files);
    static std::string generateHTMLHeader(const std::string& requestPath);
    static std::string generateParentDirectoryLink(const std::string& requestPath);
    // Wrapper structs to avoid swappable parameters
    struct RequestPath {
        std::string value;
        explicit RequestPath(const std::string& path) : value(path) {}
    };
    
    struct DirPath {
        std::string value;
        explicit DirPath(const std::string& path) : value(path) {}
    };
    
    struct DirectoryList {
        std::vector<std::string> value;
        explicit DirectoryList(const std::vector<std::string>& dirs) : value(dirs) {}
    };
    
    struct FileList {
        std::vector<std::string> value;
        explicit FileList(const std::vector<std::string>& files) : value(files) {}
    };
    
    static std::string generateDirectoryEntries(const RequestPath& requestPath, const DirPath& dirPath,
                                         const DirectoryList& directories);
    static std::string generateFileEntries(const RequestPath& requestPath, const DirPath& dirPath,
                                    const FileList& files);
    static std::string generateDirectoryHTML(const RequestPath& requestPath, const DirPath& dirPath,
                                      const DirectoryList& directories, const FileList& files);
    static std::string formatFileSize(off_t fileSize);
};

/* @------------------------------------------------------------------------@ */
/* |                            Function Section                            | */
/* @------------------------------------------------------------------------@ */

#endif
