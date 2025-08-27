/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   test_httprequest.cpp                               :+:      :+:    :+:   */
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

#include "../include/HttpRequest.hpp"
#include "../include/Logger.hpp"

// C++98 compatible to_string replacement
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

bool testBasicGET() {
    printTestHeader("Basic GET Request");
    
    Logger logger(std::cout, true);
    HttpRequest request(logger);
    
    std::string rawRequest = "GET /index.html HTTP/1.1\r\n"
                           "Host: localhost:8080\r\n"
                           "User-Agent: Mozilla/5.0\r\n"
                           "Accept: text/html\r\n"
                           "\r\n";
    
    bool parseResult = request.parse(rawRequest);
    
    bool success = parseResult && 
                  request.isComplete() && 
                  request.isValid() &&
                  request.getMethod() == "GET" &&
                  request.getPath() == "/index.html" &&
                  request.getVersion() == "HTTP/1.1" &&
                  request.getHeader("Host") == "localhost:8080" &&
                  request.getContentLength() == 0 &&
                  request.getBody().empty();
    
    std::cout << "Method: '" << request.getMethod() << "'" << std::endl;
    std::cout << "Path: '" << request.getPath() << "'" << std::endl;
    std::cout << "Version: '" << request.getVersion() << "'" << std::endl;
    std::cout << "Host header: '" << request.getHeader("Host") << "'" << std::endl;
    
    printResult(success, "Basic GET parsing");
    return success;
}

bool testPOSTWithBody() {
    printTestHeader("POST Request with Body");
    
    Logger logger(std::cout, true);
    HttpRequest request(logger);
    
    std::string postData = "name=John&email=john@example.com";
    std::string rawRequest = "POST /api/submit HTTP/1.1\r\n"
                           "Host: localhost:8080\r\n"
                           "Content-Type: application/x-www-form-urlencoded\r\n"
                           "Content-Length: " + toString(postData.length()) + "\r\n"
                           "\r\n" + postData;
    
    bool parseResult = request.parse(rawRequest);
    
    bool success = parseResult && 
                  request.getMethod() == "POST" &&
                  request.getPath() == "/api/submit" &&
                  request.getContentLength() == postData.length() &&
                  request.getBody() == postData &&
                  request.getHeader("Content-Type") == "application/x-www-form-urlencoded";
    
    std::cout << "Method: '" << request.getMethod() << "'" << std::endl;
    std::cout << "Content-Length: " << request.getContentLength() << std::endl;
    std::cout << "Body: '" << request.getBody() << "'" << std::endl;
    
    printResult(success, "POST with body parsing");
    return success;
}

bool testDELETERequest() {
    printTestHeader("DELETE Request");
    
    Logger logger(std::cout, true);
    HttpRequest request(logger);
    
    std::string rawRequest = "DELETE /api/user/123 HTTP/1.1\r\n"
                           "Host: api.example.com\r\n"
                           "Authorization: Bearer token123\r\n"
                           "\r\n";
    
    bool parseResult = request.parse(rawRequest);
    
    bool success = parseResult && 
                  request.getMethod() == "DELETE" &&
                  request.getPath() == "/api/user/123" &&
                  request.getHeader("Authorization") == "Bearer token123";
    
    std::cout << "Method: '" << request.getMethod() << "'" << std::endl;
    std::cout << "Path: '" << request.getPath() << "'" << std::endl;
    std::cout << "Authorization: '" << request.getHeader("Authorization") << "'" << std::endl;
    
    printResult(success, "DELETE request parsing");
    return success;
}

bool testCaseInsensitiveHeaders() {
    printTestHeader("Case-Insensitive Headers");
    
    Logger logger(std::cout, false);  // Disable debug for cleaner output
    HttpRequest request(logger);
    
    std::string rawRequest = "GET /test HTTP/1.1\r\n"
                           "HOST: example.com\r\n"
                           "User-agent: TestAgent/1.0\r\n"
                           "content-type: text/html\r\n"
                           "ACCEPT: */*\r\n"
                           "\r\n";
    
    bool parseResult = request.parse(rawRequest);
    
    // Test all case variations return the same value
    bool success = parseResult &&
                  request.getHeader("HOST") == "example.com" &&
                  request.getHeader("host") == "example.com" &&
                  request.getHeader("Host") == "example.com" &&
                  request.getHeader("User-Agent") == "TestAgent/1.0" &&
                  request.getHeader("user-agent") == "TestAgent/1.0" &&
                  request.getHeader("Content-Type") == "text/html" &&
                  request.getHeader("content-type") == "text/html" &&
                  request.getHeader("Accept") == "*/*" &&
                  request.getHeader("ACCEPT") == "*/*";
    
    std::cout << "HOST/host/Host: '" << request.getHeader("host") << "'" << std::endl;
    std::cout << "User-Agent/user-agent: '" << request.getHeader("user-agent") << "'" << std::endl;
    std::cout << "Content-Type/content-type: '" << request.getHeader("content-type") << "'" << std::endl;
    
    printResult(success, "Case-insensitive header access");
    return success;
}

bool testInvalidRequests() {
    printTestHeader("Invalid Requests Validation");
    
    Logger logger(std::cout, false);
    HttpRequest request;
    int passedTests = 0;
    
    // Test 1: Invalid method
    std::string invalidMethod = "INVALID /path HTTP/1.1\r\n\r\n";
    bool result1 = !request.parse(invalidMethod);  // Should fail
    std::cout << "Invalid method 'INVALID': " << (result1 ? "REJECTED ✓" : "ACCEPTED ✗") << std::endl;
    if (result1) passedTests++;
    
    // Test 2: Missing HTTP version
    request.clear();
    std::string missingVersion = "GET /path\r\n\r\n";
    bool result2 = !request.parse(missingVersion);  // Should fail
    std::cout << "Missing HTTP version: " << (result2 ? "REJECTED ✓" : "ACCEPTED ✗") << std::endl;
    if (result2) passedTests++;
    
    // Test 3: Invalid path (doesn't start with /)
    request.clear();
    std::string invalidPath = "GET invalid-path HTTP/1.1\r\n\r\n";
    bool result3 = !request.parse(invalidPath);  // Should fail
    std::cout << "Invalid path format: " << (result3 ? "REJECTED ✓" : "ACCEPTED ✗") << std::endl;
    if (result3) passedTests++;
    
    // Test 4: Incomplete request (no \\r\\n\\r\\n)
    request.clear();
    std::string incomplete = "GET /path HTTP/1.1\r\nHost: localhost";
    bool result4 = !request.parse(incomplete);  // Should fail
    std::cout << "Incomplete request: " << (result4 ? "REJECTED ✓" : "ACCEPTED ✗") << std::endl;
    if (result4) passedTests++;
    
    // Test 5: Body size mismatch
    request.clear();
    std::string mismatch = "POST /test HTTP/1.1\r\n"
                          "Content-Length: 20\r\n"
                          "\r\n"
                          "short";  // Only 5 bytes
    bool result5 = !request.parse(mismatch);  // Should fail
    std::cout << "Body size mismatch: " << (result5 ? "REJECTED ✓" : "ACCEPTED ✗") << std::endl;
    if (result5) passedTests++;
    
    bool allPassed = (passedTests == 5);
    printResult(allPassed, "Invalid request validation (" + toString(passedTests) + "/5)");
    return allPassed;
}

bool testEdgeCases() {
    printTestHeader("Edge Cases");
    
    Logger logger(std::cout, false);
    HttpRequest request;
    int passedTests = 0;
    
    // Test 1: Empty headers value
    std::string emptyHeaderValue = "GET /test HTTP/1.1\r\n"
                                 "Host: localhost\r\n"
                                 "Custom-Header: \r\n"
                                 "\r\n";
    if (request.parse(emptyHeaderValue) && request.getHeader("Custom-Header").empty()) {
        std::cout << "Empty header value: HANDLED ✓" << std::endl;
        passedTests++;
    } else {
        std::cout << "Empty header value: FAILED ✗" << std::endl;
    }
    
    // Test 2: Long path
    request.clear();
    std::string longPath = "/very/long/path/with/many/segments/and/parameters?param1=value1&param2=value2&param3=value3";
    std::string longPathRequest = "GET " + longPath + " HTTP/1.1\r\n"
                                "Host: localhost\r\n\r\n";
    if (request.parse(longPathRequest) && request.getPath() == longPath) {
        std::cout << "Long path handling: HANDLED ✓" << std::endl;
        passedTests++;
    } else {
        std::cout << "Long path handling: FAILED ✗" << std::endl;
    }
    
    // Test 3: Zero content length
    request.clear();
    std::string zeroLength = "POST /submit HTTP/1.1\r\n"
                           "Content-Length: 0\r\n"
                           "\r\n";
    if (request.parse(zeroLength) && request.getContentLength() == 0 && request.getBody().empty()) {
        std::cout << "Zero Content-Length: HANDLED ✓" << std::endl;
        passedTests++;
    } else {
        std::cout << "Zero Content-Length: FAILED ✗" << std::endl;
    }
    
    bool allPassed = (passedTests == 3);
    printResult(allPassed, "Edge cases (" + toString(passedTests) + "/3)");
    return allPassed;
}

int main() {
    std::cout << "=====================================================" << std::endl;
    std::cout << "           HttpRequest Comprehensive Test Suite     " << std::endl;
    std::cout << "=====================================================" << std::endl;
    
    int totalTests = 0;
    int passedTests = 0;
    
    // Run all tests
    if (testBasicGET()) passedTests++;
    totalTests++;
    
    if (testPOSTWithBody()) passedTests++;
    totalTests++;
    
    if (testDELETERequest()) passedTests++;
    totalTests++;
    
    if (testCaseInsensitiveHeaders()) passedTests++;
    totalTests++;
    
    if (testInvalidRequests()) passedTests++;
    totalTests++;
    
    if (testEdgeCases()) passedTests++;
    totalTests++;
    
    // Final summary
    std::cout << "\n=====================================================" << std::endl;
    std::cout << "                    TEST SUMMARY                     " << std::endl;
    std::cout << "=====================================================" << std::endl;
    std::cout << "Tests Passed: " << passedTests << "/" << totalTests << std::endl;
    
    if (passedTests == totalTests) {
        std::cout << "🎉 ALL TESTS PASSED! HttpRequest is ready for production." << std::endl;
    } else {
        std::cout << "❌ Some tests failed. Review the implementation." << std::endl;
    }
    
    std::cout << "=====================================================" << std::endl;
    
    return (passedTests == totalTests) ? 0 : 1;
}
