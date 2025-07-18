/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Logger.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ribana-b <ribana-b@student.42malaga.com>   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/07/06 00:01:10 by ribana-b          #+#    #+# Malaga      */
/*   Updated: 2025/07/18 12:42:51 by ribana-b         ###   ########.com      */
/*                                                                            */
/* ************************************************************************** */

#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <ostream>  // For std::ostream
#include <sstream>  // For std::ostringstream
#include <string>   // For std::string

class Logger {
public:
    class LoggerStream {
    public:
        LoggerStream(std::ostream& out, const std::string& prefix);
        ~LoggerStream();
        LoggerStream(const LoggerStream& that);
        LoggerStream& operator=(const LoggerStream& that);

        template <typename T>
        LoggerStream& operator<<(const T& value) {
            m_Oss << value;
            return (*this);
        }

    private:
        std::ostream&      m_Out;
        std::ostringstream m_Oss;
        std::string        m_Prefix;
    };

    Logger(std::ostream& out, bool enableColour);
    Logger(std::ostream& out);
    Logger();
    ~Logger();
    Logger(const Logger& that);
    Logger& operator=(const Logger& that);

    LoggerStream debug();
    LoggerStream info();
    LoggerStream warn();
    LoggerStream error();

private:
    std::ostream& m_Out;
    bool          m_EnableColour;
};

#endif
