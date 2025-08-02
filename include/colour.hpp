/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   colour.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ribana-b <ribana-b@student.42malaga.com>   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/07/18 09:35:35 by ribana-b          #+#    #+# Malaga      */
/*   Updated: 2025/07/24 15:58:21 by ribana-b         ###   ########.com      */
/*                                                                            */
/* ************************************************************************** */

#ifndef COLOUR_HPP
#define COLOUR_HPP

#include <stdint.h>  // For int types

#include <string>  // For std::string

namespace colour {
    std::string red(const std::string& text);
    std::string green(const std::string& text);
    std::string yellow(const std::string& text);
    std::string cyan(const std::string& text);
    std::string rgb(const std::string& text, uint8_t r, uint8_t g, uint8_t b);
}  // namespace colour

#endif
