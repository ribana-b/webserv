/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   colour.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ribana-b <ribana-b@student.42malaga.com>   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/07/18 09:44:12 by ribana-b          #+#    #+# Malaga      */
/*   Updated: 2025/07/18 12:42:07 by ribana-b         ###   ########.com      */
/*                                                                            */
/* ************************************************************************** */

#include "colour.hpp"

#include <stdint.h>  // For int types

#include <sstream>  // For std::ostreamstring
#include <string>   // For std::string

std::string colour::red(const std::string& text) { return ("\033[31m" + text + "\033[0m"); }

std::string colour::green(const std::string& text) { return ("\033[32m" + text + "\033[0m"); }

std::string colour::yellow(const std::string& text) { return ("\033[33m" + text + "\033[0m"); }

std::string colour::cyan(const std::string& text) { return ("\033[36m" + text + "\033[0m"); }

std::string colour::rgb(const std::string& text, uint8_t r, uint8_t g, uint8_t b) {  // NOLINT
    std::ostringstream oss;

    oss << "\033[38;2;";
    oss << static_cast<int>(r) << ";";
    oss << static_cast<int>(g) << ";";
    oss << static_cast<int>(b);
    oss << "m";
    return (oss.str() + text + "\033[0m");
}
