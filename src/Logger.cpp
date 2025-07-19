/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Logger.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ribana-b <ribana-b@student.42malaga.com>   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/07/06 00:03:38 by ribana-b          #+#    #+# Malaga      */
/*   Updated: 2025/07/18 12:41:38 by ribana-b         ###   ########.com      */
/*                                                                            */
/* ************************************************************************** */

#include "Logger.hpp"

#include <iostream>  // For std::cout
#include <ostream>   // For std::ostream

#include "colour.hpp"

/* @------------------------------------------------------------------------@ */
/* |                             Logger Section                             | */
/* @------------------------------------------------------------------------@ */

Logger::Logger(std::ostream& out, bool enableColour) : m_Out(out), m_EnableColour(enableColour) {}

Logger::Logger(std::ostream& out) : m_Out(out), m_EnableColour(false) {}

Logger::Logger() : m_Out(std::cout), m_EnableColour(true) {}

Logger::~Logger() {}

Logger::Logger(const Logger& that) : m_Out(that.m_Out), m_EnableColour(that.m_EnableColour) {}

Logger& Logger::operator=(const Logger& that) {
    (void)that;
    return (*this);
}

Logger::LoggerStream Logger::debug() {
    if (m_EnableColour) {
        return (LoggerStream(m_Out, colour::cyan("[DEBUG] ")));
    }
    return (LoggerStream(m_Out, "[DEBUG] "));
}
Logger::LoggerStream Logger::info() {
    if (m_EnableColour) {
        return (LoggerStream(m_Out, colour::green("[INFO]  ")));
    }
    return (LoggerStream(m_Out, "[INFO] "));
}

Logger::LoggerStream Logger::warn() {
    if (m_EnableColour) {
        return (LoggerStream(m_Out, colour::yellow("[WARN]  ")));
    }
    return (LoggerStream(m_Out, "[WARN] "));
}

Logger::LoggerStream Logger::error() {
    if (m_EnableColour) {
        return (LoggerStream(m_Out, colour::red("[ERROR] ")));
    }
    return (LoggerStream(m_Out, "[ERROR] "));
}

/* @------------------------------------------------------------------------@ */
/* |                          LoggerStream Section                          | */
/* @------------------------------------------------------------------------@ */

Logger::LoggerStream::LoggerStream(std::ostream& out, const std::string& prefix) :
    m_Out(out), m_Prefix(prefix) {}

Logger::LoggerStream::~LoggerStream() { m_Out << m_Prefix << m_Oss.str() << std::endl; }

Logger::LoggerStream::LoggerStream(const LoggerStream& that) :
    m_Out(that.m_Out), m_Prefix(that.m_Prefix) {}

Logger::LoggerStream& Logger::LoggerStream::operator=(const LoggerStream& that) {
    (void)that;
    return (*this);
}
