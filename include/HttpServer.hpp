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
    bool         testIsPathSafe(const std::string& path) const;
    std::string  testResolvePath(const std::string&      requestPath,
                                 const Config::Location& location) const;
    std::string  testJoinPath(const std::string& base, const std::string& path) const;

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
    const Config::Location* findMatchingLocation(const Config::Server& server,
                                                 const std::string&    path) const;

    bool        isMethodAllowed(const std::string& method, const Config::Location& location) const;
    bool        isPathSafe(const std::string& path) const;
    bool        isCGIFile(const std::string& filePath) const;
    std::string resolvePath(const std::string& requestPath, const Config::Location& location) const;
    std::string joinPath(const std::string& base, const std::string& path) const;

    HttpResponse createErrorResponse(int statusCode, const Config::Server& server);
};

/* @------------------------------------------------------------------------@ */
/* |                            Function Section                            | */
/* @------------------------------------------------------------------------@ */

#endif
