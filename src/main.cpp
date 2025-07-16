/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   main.cpp                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: ribana-b <ribana-b@student.42malaga.com>   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/06/02 11:42:05 by ribana-b          #+#    #+# Malaga      */
/*   Updated: 2025/07/16 17:36:08 by disantam         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include <iostream>
#include "monitor.hpp"
#include "webserv.hpp"

int    execMonitor(char **argv, int argc)
{
    Monitor monitor;

    if (monitor.init(argv + 1, argc - 1) < 0)
    {
        return -1;
    }
    monitor.beginLoop();
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    { 
        return 1; 
    }
    execMonitor(argv, argc);
    std::cout << "Hello webserv" << std::endl;
    return (0);
}

