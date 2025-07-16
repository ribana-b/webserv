/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   monitor.hpp                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: disantam <disantam@student.42malaga.com>   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/07/15 13:37:28 by disantam          #+#    #+#             */
/*   Updated: 2025/07/16 17:56:34 by disantam         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef MONITOR_HPP
# define MONITOR_HPP

# define POLLFD_SIZE 10
# define LISTEN_BACKLOG 10
# define POLL_WAIT 30000
# define BUFFER_SIZE 500

/* @------------------------------------------------------------------------@ */
/* |                            Include Section                             | */
/* @------------------------------------------------------------------------@ */

/* @------------------------------------------------------------------------@ */
/* |                             Class Section                              | */
/* @------------------------------------------------------------------------@ */

struct  pollfd;

class Monitor
{
private:
    struct pollfd   *fds;
    int             *listenFds;
    int             listenCount;
    int             fdCount;
    int             maxFd;

    enum InitResult
    {
        INIT_SUCCESS,
        INIT_MEMORY_ERROR,
        INIT_LISTEN_ERROR
    };

    enum ExecResult
    {
        EXEC_SUCCESS,
        EXEC_CONNECTION_ERROR,
        EXEC_FATAL_ERROR
    };

    void        addPollFd(int fdesc);
    void        closePollFd(int fdesc);
    void        cleanPollFds();
    int         isPollFd(int fdesc) const;
    int         isListenFd(int fdesc) const;
    InitResult  initData(char **ports, int n);
    static int  initListenFd(struct sockaddr_in &address);
    int         eventInit(int ready);
    int         eventExec(int fdesc, int &ready);
    ExecResult  eventExecType(int fdesc, int &ready);
    ExecResult  eventExecConnection(int fdesc, int &ready);
    ExecResult  eventExecRequest(int fdesc, int &ready);

public:
    Monitor();
    ~Monitor();

    int     init(char **ports, int n);
    void    beginLoop();
};

/* @------------------------------------------------------------------------@ */
/* |                            Function Section                            | */
/* @------------------------------------------------------------------------@ */

#endif

