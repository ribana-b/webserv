# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a C++98 HTTP web server implementation developed as part of the 42 curriculum. The server handles HTTP/1.0 requests, serves static files, manages multiple virtual servers, and supports CGI execution.

## Build System and Commands

### Primary Commands
- `make` or `make all` - Build the webserv executable
- `make clean` - Remove object files
- `make fclean` - Remove all generated files including executable
- `make re` - Clean rebuild
- `make quiet` - Silent build with `make -s QUIET=1`

### Code Quality Tools (Docker-based)
- `make format` - Check code formatting with clang-format (dry-run)
- `make apply-format` - Apply clang-format to all source files
- `make tidy` - Run clang-tidy static analysis
- Set `NODOCKER=1` to run tools without Docker

### Execution
```bash
./webserv [config_file]
```
If no config file is provided, looks for `default.conf` in the current directory.

## Architecture Overview

### Core Components

**Config System** (`Config.hpp/cpp`):
- Parses NGINX-like configuration files
- Manages multiple server blocks with different ports/settings
- Handles location routing, error pages, file upload settings
- Key structures: `Config::Server`, `Config::Location`, `Config::Listen`

**Monitor System** (`Monitor.hpp/cpp` + `MonitorInit.cpp` + `MonitorEvent.cpp`):
- Non-blocking I/O multiplexing using poll() (or select/kqueue/epoll)
- Manages listening sockets and client connections
- Event-driven architecture handling connection acceptance and HTTP requests
- Constants: `POLLFD_SIZE=10`, `LISTEN_BACKLOG=10`, `POLL_WAIT=30000ms`, `BUFFER_SIZE=500`

**Logger System** (`Logger.hpp/cpp`):
- Centralized logging with color support
- Used throughout the application for debugging and monitoring

### HTTP Server Flow
1. **Initialization**: Config parser reads server blocks, Monitor creates listening sockets
2. **Event Loop**: Monitor.beginLoop() polls for events on all file descriptors
3. **Connection Handling**: New connections accepted, HTTP requests parsed and responded to
4. **Resource Serving**: Static files served from configured document roots

### Configuration Format
Follows NGINX-like syntax with server blocks:
```nginx
server {
    listen 4242;
    root /var/www/html;
    index index.html;
    error_page 404 /404.html;
    
    location / {
        root /var/www/html;
        autoindex on;
        allow_methods GET POST DELETE;
        client_max_body_size 2M;
    }
}
```

## Development Standards

### C++98 Compliance
- Strict C++98 standard (`-std=c++98`)
- Compiler flags: `-Wall -Wextra -Werror`
- No external libraries beyond standard C++98 and system calls
- Prefer C++ headers (`<cstring>` over `<string.h>`)

### System Requirements
- Must be non-blocking and use single poll() call for all I/O
- Cannot execve other web servers
- Must handle client disconnections gracefully
- Support GET, POST, DELETE methods
- File upload capability required
- CGI support based on file extensions

### Allowed System Calls
execve, pipe, strerror, gai_strerror, errno, dup, dup2, fork, socketpair, htons, htonl, ntohs, ntohl, select, poll, epoll, kqueue, socket, accept, listen, send, recv, chdir, bind, connect, getaddrinfo, freeaddrinfo, setsockopt, getsockname, getprotobyname, fcntl, close, read, write, waitpid, kill, signal, access, stat, open, opendir, readdir, closedir

## Key Implementation Notes

- Log file written to `webserv.log`
- Default config searched relative to executable location
- Poll-based architecture prevents blocking operations
- Multi-server support through configuration
- Static website serving with directory listing support
- Error page customization per server block
- Location-based routing with method restrictions