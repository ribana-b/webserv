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
    static std::string joinPath(const std::string& base, const std::string& path);

    HttpResponse createErrorResponse(int statusCode, const Config::Server& server);
};

/* @------------------------------------------------------------------------@ */
/* |                            Function Section                            | */
/* @------------------------------------------------------------------------@ */

#endif
