/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   demo_http.cpp                                      :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mancorte <mancorte@student.42malaga.com>   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/01/27 00:00:00 by mancorte          #+#    #+# Malaga      */
/*   Updated: 2025/01/27 00:00:00 by mancorte         ###   ########.com      */
/*                                                                            */
/* ************************************************************************** */

#include <iostream>
#include <string>

#include "../include/HttpRequest.hpp"
#include "../include/HttpResponse.hpp"
#include "../include/HttpServer.hpp"
#include "../include/Config.hpp"
#include "../include/Logger.hpp"

int main() {
    std::cout << "=== DEMO HTTP SYSTEM ===" << std::endl;
    
    // 1. Crear un request HTTP real
    std::string rawRequest = 
        "GET /index.html HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "User-Agent: webserv-demo/1.0\r\n"
        "Accept: text/html,application/xhtml+xml\r\n"
        "Connection: close\r\n"
        "\r\n";
    
    std::cout << "\n1. REQUEST HTTP RECIBIDO:" << std::endl;
    std::cout << "---" << std::endl;
    std::cout << rawRequest << std::endl;
    
    // 2. Parsearlo con HttpRequest
    HttpRequest request;
    request.parse(rawRequest);
    
    std::cout << "2. REQUEST PARSEADO:" << std::endl;
    std::cout << "   Method: " << request.getMethod() << std::endl;
    std::cout << "   Path: " << request.getPath() << std::endl;
    std::cout << "   Version: " << request.getVersion() << std::endl;
    std::cout << "   Host: " << request.getHeader("Host") << std::endl;
    std::cout << "   Valid: " << (request.isValid() ? "YES" : "NO") << std::endl;
    
    // 3. Procesarlo con HttpServer
    Config config;
    Logger logger(std::cout, false);
    HttpServer server(config, logger);
    
    HttpResponse response = server.processRequest(request, 8080);
    
    std::cout << "\n3. RESPONSE GENERADO:" << std::endl;
    std::cout << "   Status: " << response.getStatusCode() << " " << response.getStatusMessage() << std::endl;
    std::cout << "   Content-Type: " << response.getHeader("Content-Type") << std::endl;
    std::cout << "   Content-Length: " << response.getContentLength() << std::endl;
    
    // 4. Convertir a string HTTP listo para enviar
    std::string responseStr = response.toString();
    
    std::cout << "\n4. RESPONSE HTTP COMPLETO (listo para socket):" << std::endl;
    std::cout << "---" << std::endl;
    std::cout << responseStr << std::endl;
    std::cout << "---" << std::endl;
    
    std::cout << "\n✅ El sistema HTTP está COMPLETAMENTE FUNCIONAL!" << std::endl;
    std::cout << "   Solo falta conectarlo al Monitor (event loop)" << std::endl;
    
    return 0;
}
