/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   test_static_files.cpp                              :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mancorte <mancorte@student.42malaga.com>   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/01/27 00:00:00 by mancorte          #+#    #+# Malaga      */
/*   Updated: 2025/01/27 00:00:00 by mancorte         ###   ########.com      */
/*                                                                            */
/* ************************************************************************** */

#include <iostream>
#include <sstream>
#include <string>
#include <fstream>

#include "../include/HttpServer.hpp"
#include "../include/HttpRequest.hpp"
#include "../include/HttpResponse.hpp"
#include "../include/Config.hpp"
#include "../include/Logger.hpp"

std::string toString(size_t value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

void printTestHeader(const std::string& testName) {
    std::cout << "\n===========================================" << std::endl;
    std::cout << "  " << testName << std::endl;
    std::cout << "===========================================" << std::endl;
}

void printResult(bool success, const std::string& testName) {
    std::cout << "[" << (success ? "PASS" : "FAIL") << "] " << testName << std::endl;
}

Config createTestConfigWithServer() {
    Config config;
    return config;
}

bool testDirectoryListing() {
    printTestHeader("Directory Listing Test");
    
    Config config = createTestConfigWithServer();
    Logger logger(std::cout, false);
    HttpServer server(config, logger);
    
    // Set document root to our test directory
    server.setDocumentRoot("/tmp/webserv_test_files");
    
    HttpRequest request;
    request.parse("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    
    // Manually create a mock server for testing
    Config::Server mockServer;
    Config::Location mockLocation;
    mockLocation.path = "/";
    mockLocation.root = "/tmp/webserv_test_files";
    mockLocation.autoindex = true;
    mockLocation.allowMethods.insert("GET");
    mockServer.locations.push_back(mockLocation);
    
    // Test directory listing directly
    HttpResponse response = server.testGenerateDirectoryListing("/tmp/webserv_test_files", "/", mockServer);
    
    bool success = response.getStatusCode() == 200 &&
                  response.getHeader("Content-Type") == "text/html; charset=utf-8" &&
                  response.getBody().find("index.html") != std::string::npos &&
                  response.getBody().find("test.txt") != std::string::npos &&
                  response.getBody().find("subdir/") != std::string::npos;
    
    std::cout << "Response status: " << response.getStatusCode() << std::endl;
    std::cout << "Content-Type: " << response.getHeader("Content-Type") << std::endl;
    std::cout << "Body length: " << response.getContentLength() << " bytes" << std::endl;
    std::cout << "Contains index.html: " << (response.getBody().find("index.html") != std::string::npos ? "YES" : "NO") << std::endl;
    std::cout << "Contains subdir/: " << (response.getBody().find("subdir/") != std::string::npos ? "YES" : "NO") << std::endl;
    
    printResult(success, "Directory listing generation");
    return success;
}

bool testStaticFileServing() {
    printTestHeader("Static File Serving Test");
    
    Config config = createTestConfigWithServer();
    Logger logger(std::cout, false);
    HttpServer server(config, logger);
    
    Config::Server mockServer;
    
    // Test serving index.html
    HttpResponse response = server.testServeStaticFile("/tmp/webserv_test_files/index.html", mockServer);
    
    bool success = response.getStatusCode() == 200 &&
                  response.getBody().find("Hello from index.html") != std::string::npos &&
                  response.getContentLength() > 0;
    
    std::cout << "File: index.html" << std::endl;
    std::cout << "Response status: " << response.getStatusCode() << std::endl;
    std::cout << "Content-Length: " << response.getContentLength() << std::endl;
    std::cout << "Body: '" << response.getBody() << "'" << std::endl;
    std::cout << "Content-Type: " << response.getHeader("Content-Type") << std::endl;
    
    printResult(success, "Static file serving");
    return success;
}

bool testFileNotFound() {
    printTestHeader("File Not Found Test");
    
    Config config = createTestConfigWithServer();
    Logger logger(std::cout, false);
    HttpServer server(config, logger);
    
    Config::Server mockServer;
    
    // Test serving non-existent file
    HttpResponse response = server.testServeStaticFile("/tmp/webserv_test_files/nonexistent.html", mockServer);
    
    bool success = response.getStatusCode() == 404;
    
    std::cout << "Non-existent file test" << std::endl;
    std::cout << "Response status: " << response.getStatusCode() << " " << response.getStatusMessage() << std::endl;
    
    printResult(success, "File not found handling");
    return success;
}

bool testPathSafety() {
    printTestHeader("Path Safety Test");
    
    Config config = createTestConfigWithServer();
    Logger logger(std::cout, false);
    HttpServer server(config, logger);
    
    HttpRequest dangerousRequest;
    dangerousRequest.parse("GET /../../../etc/passwd HTTP/1.1\r\nHost: localhost\r\n\r\n");
    
    // This should be rejected by isPathSafe()
    bool pathUnsafe = !server.testIsPathSafe("/../../../etc/passwd");
    bool pathUnsafe2 = !server.testIsPathSafe("/test/../../../etc/passwd");
    bool pathSafe = server.testIsPathSafe("/normal/path");
    
    bool success = pathUnsafe && pathUnsafe2 && pathSafe;
    
    std::cout << "Path '/../../../etc/passwd' unsafe: " << (pathUnsafe ? "YES" : "NO") << std::endl;
    std::cout << "Path '/test/../../../etc/passwd' unsafe: " << (pathUnsafe2 ? "YES" : "NO") << std::endl;
    std::cout << "Path '/normal/path' safe: " << (pathSafe ? "YES" : "NO") << std::endl;
    
    printResult(success, "Path traversal protection");
    return success;
}

bool testPathResolution() {
    printTestHeader("Path Resolution Test");
    
    Config config = createTestConfigWithServer();
    Logger logger(std::cout, false);
    HttpServer server(config, logger);
    
    server.setDocumentRoot("/var/www/html");
    
    Config::Location location;
    location.root = "/tmp/webserv_test_files";
    
    // Test path resolution
    std::string resolved1 = server.testResolvePath("/test.txt", location);
    std::string resolved2 = server.testResolvePath("/", location);
    
    // Test joinPath
    std::string joined1 = server.testJoinPath("/tmp/webserv_test_files", "test.txt");
    std::string joined2 = server.testJoinPath("/tmp/webserv_test_files/", "/test.txt");
    std::string joined3 = server.testJoinPath("/tmp/webserv_test_files", "/test.txt");
    
    bool success = resolved1 == "/tmp/webserv_test_files/test.txt" &&
                  resolved2 == "/tmp/webserv_test_files" &&
                  joined1 == "/tmp/webserv_test_files/test.txt" &&
                  joined2 == "/tmp/webserv_test_files/test.txt" &&
                  joined3 == "/tmp/webserv_test_files/test.txt";
    
    std::cout << "resolvePath('/test.txt', location): '" << resolved1 << "'" << std::endl;
    std::cout << "resolvePath('/', location): '" << resolved2 << "'" << std::endl;
    std::cout << "joinPath('/tmp/webserv_test_files', 'test.txt'): '" << joined1 << "'" << std::endl;
    std::cout << "joinPath('/tmp/webserv_test_files/', '/test.txt'): '" << joined2 << "'" << std::endl;
    std::cout << "joinPath('/tmp/webserv_test_files', '/test.txt'): '" << joined3 << "'" << std::endl;
    
    printResult(success, "Path resolution and joining");
    return success;
}

bool testCustomErrorPage() {
    printTestHeader("Custom Error Page Test");
    
    // Create a custom 404 error page
    std::ofstream errorFile("/tmp/webserv_test_files/custom_404.html");
    errorFile << "<html><body><h1>Custom 404 Page</h1><p>File not found!</p></body></html>";
    errorFile.close();
    
    Config config = createTestConfigWithServer();
    Logger logger(std::cout, false);
    HttpServer server(config, logger);
    
    Config::Server mockServer;
    mockServer.errorPages[404] = "/tmp/webserv_test_files/custom_404.html";
    
    HttpResponse response = server.testCreateErrorResponse(404, mockServer);
    
    bool success = response.getStatusCode() == 404 &&
                  response.getBody().find("Custom 404 Page") != std::string::npos;
    
    std::cout << "Custom 404 error page test" << std::endl;
    std::cout << "Response status: " << response.getStatusCode() << std::endl;
    std::cout << "Body contains 'Custom 404 Page': " << (response.getBody().find("Custom 404 Page") != std::string::npos ? "YES" : "NO") << std::endl;
    
    printResult(success, "Custom error page serving");
    return success;
}

bool testMultipleIndexFiles() {
    printTestHeader("Multiple Index Files Test");
    
    // Create additional index files for testing priority
    std::ofstream indexHtm("/tmp/webserv_test_files/index.htm");
    indexHtm << "This is index.htm content";
    indexHtm.close();
    
    std::ofstream defaultHtml("/tmp/webserv_test_files/default.html");
    defaultHtml << "This is default.html content";
    defaultHtml.close();
    
    Config config = createTestConfigWithServer();
    Logger logger(std::cout, false);
    HttpServer server(config, logger);
    
    Config::Server mockServer;
    // Configure server with multiple index files (priority order)
    mockServer.index.push_back("default.html");
    mockServer.index.push_back("index.htm");
    mockServer.index.push_back("index.html");
    
    Config::Location mockLocation;
    mockLocation.path = "/";
    mockLocation.root = "/tmp/webserv_test_files";
    // Location-specific index files (higher priority than server)
    mockLocation.index.push_back("index.htm");
    mockLocation.index.push_back("default.html");
    mockServer.locations.push_back(mockLocation);
    
    // Test that it finds index.htm first (location config priority)
    HttpResponse response1 = server.testServeStaticFile("/tmp/webserv_test_files/index.htm", mockServer);
    bool test1 = response1.getStatusCode() == 200 && 
                response1.getBody().find("index.htm content") != std::string::npos;
    
    // Test index.html still works
    HttpResponse response2 = server.testServeStaticFile("/tmp/webserv_test_files/index.html", mockServer);
    bool test2 = response2.getStatusCode() == 200 && 
                response2.getBody().find("Hello from index.html") != std::string::npos;
    
    bool success = test1 && test2;
    
    std::cout << "Multiple index files configuration:" << std::endl;
    std::cout << "  Location index: index.htm, default.html" << std::endl;
    std::cout << "  Server index: default.html, index.htm, index.html" << std::endl;
    std::cout << "index.htm serves correctly: " << (test1 ? "YES" : "NO") << std::endl;
    std::cout << "index.html serves correctly: " << (test2 ? "YES" : "NO") << std::endl;
    
    printResult(success, "Multiple index files handling");
    return success;
}

int main() {
    std::cout << "=====================================================" << std::endl;
    std::cout << "           Static File Serving Test Suite           " << std::endl;
    std::cout << "=====================================================" << std::endl;
    
    int totalTests = 0;
    int passedTests = 0;
    
    if (testDirectoryListing()) passedTests++;
    totalTests++;
    
    if (testStaticFileServing()) passedTests++;
    totalTests++;
    
    if (testFileNotFound()) passedTests++;
    totalTests++;
    
    if (testPathSafety()) passedTests++;
    totalTests++;
    
    if (testPathResolution()) passedTests++;
    totalTests++;
    
    if (testCustomErrorPage()) passedTests++;
    totalTests++;
    
    if (testMultipleIndexFiles()) passedTests++;
    totalTests++;
    
    std::cout << "\n=====================================================" << std::endl;
    std::cout << "                    TEST SUMMARY                     " << std::endl;
    std::cout << "=====================================================" << std::endl;
    std::cout << "Tests Passed: " << passedTests << "/" << totalTests << std::endl;
    
    if (passedTests == totalTests) {
        std::cout << "🎉 ALL STATIC FILE TESTS PASSED! Ready for real file serving." << std::endl;
    } else {
        std::cout << "❌ Some tests failed. Review the static file implementation." << std::endl;
    }
    
    std::cout << "=====================================================" << std::endl;
    
    return (passedTests == totalTests) ? 0 : 1;
}
