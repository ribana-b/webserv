#!/usr/bin/env python3
"""
Comprehensive test suite for webserv implementation
Based on ubuntu_tester test cases
"""

import socket
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from threading import Lock

# Configuration
HOST = 'localhost'
PORT = 1234

# Test results tracking
test_count = 0
passed_count = 0
failed_count = 0
results_lock = Lock()

class Colors:
    GREEN = '\033[32m'
    RED = '\033[31m'
    YELLOW = '\033[33m'
    BLUE = '\033[34m'
    RESET = '\033[0m'

def http_request(method, path, body=None, headers=None, timeout=30.0, use_chunked=False):
    """Send HTTP request and return response"""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        sock.connect((HOST, PORT))

        # Build request
        if headers is None:
            headers = {}

        if 'Host' not in headers:
            headers['Host'] = f'{HOST}:{PORT}'

        # Handle body encoding
        if body is not None:
            if use_chunked:
                headers['Transfer-Encoding'] = 'chunked'
                # Encode body as chunked
                chunk_size = 32768
                chunked_body = b''
                offset = 0
                while offset < len(body):
                    chunk = body[offset:offset + chunk_size]
                    chunked_body += f'{len(chunk):x}\r\n'.encode() + chunk + b'\r\n'
                    offset += chunk_size
                chunked_body += b'0\r\n\r\n'
                body_to_send = chunked_body
            else:
                if 'Content-Length' not in headers:
                    headers['Content-Length'] = str(len(body))
                body_to_send = body
        else:
            body_to_send = b''

        # Build request line and headers
        request = f'{method} {path} HTTP/1.1\r\n'
        for key, value in headers.items():
            request += f'{key}: {value}\r\n'
        request += '\r\n'

        # Send request
        sock.sendall(request.encode() + body_to_send)

        # Receive response
        response = b''
        while True:
            try:
                chunk = sock.recv(65536)
                if not chunk:
                    break
                response += chunk
            except socket.timeout:
                break

        sock.close()

        # Parse response
        if not response:
            return None, None, None

        header_end = response.find(b'\r\n\r\n')
        if header_end == -1:
            return None, None, None

        headers_bytes = response[:header_end]
        body_bytes = response[header_end + 4:]

        # Parse status line
        lines = headers_bytes.decode('latin1').split('\r\n')
        status_line = lines[0]
        status_code = int(status_line.split()[1])

        # Parse headers
        resp_headers = {}
        for line in lines[1:]:
            if ':' in line:
                key, value = line.split(':', 1)
                resp_headers[key.strip()] = value.strip()

        return status_code, resp_headers, body_bytes

    except Exception as e:
        print(f"{Colors.RED}Error in request: {e}{Colors.RESET}")
        return None, None, None

def test_result(name, passed, details=""):
    """Track and print test result"""
    global test_count, passed_count, failed_count

    with results_lock:
        test_count += 1
        if passed:
            passed_count += 1
            status = f"{Colors.GREEN}✓ PASS{Colors.RESET}"
        else:
            failed_count += 1
            status = f"{Colors.RED}✗ FAIL{Colors.RESET}"

        print(f"{status} Test {test_count}: {name}")
        if details:
            print(f"      {details}")

def test_get(path, expected_status=200, expect_404=False):
    """Test GET request"""
    status, headers, body = http_request('GET', path)

    if expect_404:
        passed = status == 404
        test_result(f"GET {path} (expect 404)", passed,
                   f"Status: {status}")
    else:
        passed = status == expected_status
        test_result(f"GET {path}", passed,
                   f"Status: {status}, Body size: {len(body) if body else 0}")

    return passed

def test_post(path, size, expect_status=200, use_chunked=False, special_headers=None):
    """Test POST request"""
    body = b'X' * size
    headers = special_headers or {}

    status, resp_headers, resp_body = http_request('POST', path, body, headers,
                                                     timeout=180.0, use_chunked=use_chunked)

    if status is None:
        test_result(f"POST {path} size={size:,}", False, "Connection failed")
        return False

    # Check Content-Length if present
    if 'Content-Length' in resp_headers:
        expected_length = int(resp_headers['Content-Length'])
        actual_length = len(resp_body)
        passed = status == expect_status and actual_length == expected_length
        test_result(f"POST {path} size={size:,}", passed,
                   f"Status: {status}, Expected body: {expected_length:,}, Actual: {actual_length:,}")
    else:
        passed = status == expect_status
        test_result(f"POST {path} size={size:,}", passed,
                   f"Status: {status}, Body size: {len(resp_body):,}")

    return passed

def test_put(path, size):
    """Test PUT request"""
    body = b'Y' * size
    status, headers, resp_body = http_request('PUT', path, body, timeout=180.0)

    passed = status in [200, 201, 204]
    test_result(f"PUT {path} size={size:,}", passed,
               f"Status: {status}")

    return passed

def test_head(path):
    """Test HEAD request"""
    status, headers, body = http_request('HEAD', path)

    # HEAD should return headers but no body
    passed = status == 200 and len(body) == 0
    test_result(f"HEAD {path}", passed,
               f"Status: {status}, Body size: {len(body)}")

    return passed

def worker_get(path, iterations):
    """Worker function for concurrent GET tests"""
    for _ in range(iterations):
        status, _, _ = http_request('GET', path, timeout=10.0)
        if status != 200:
            return False
    return True

def worker_put(path, size, iterations):
    """Worker function for concurrent PUT tests"""
    body = b'Z' * size
    for _ in range(iterations):
        status, _, _ = http_request('PUT', path, body, timeout=30.0)
        if status not in [200, 201, 204]:
            return False
    return True

def worker_post(path, size, iterations):
    """Worker function for concurrent POST tests"""
    body = b'W' * size
    for _ in range(iterations):
        status, _, _ = http_request('POST', path, body, timeout=180.0, use_chunked=True)
        if status != 200:
            return False
    return True

def test_concurrent(test_name, worker_func, workers, iterations, *args):
    """Test concurrent requests"""
    start = time.time()

    with ThreadPoolExecutor(max_workers=workers) as executor:
        futures = [executor.submit(worker_func, *args, iterations) for _ in range(workers)]

        success = 0
        failed = 0
        for future in as_completed(futures):
            if future.result():
                success += 1
            else:
                failed += 1

    duration = time.time() - start
    passed = failed == 0

    test_result(test_name, passed,
               f"Workers: {workers}, Iterations: {iterations}, Success: {success}, Failed: {failed}, Time: {duration:.2f}s")

    return passed

def main():
    """Run all tests"""
    print(f"\n{Colors.BLUE}{'='*70}{Colors.RESET}")
    print(f"{Colors.BLUE}Webserv Test Suite - Based on ubuntu_tester{Colors.RESET}")
    print(f"{Colors.BLUE}{'='*70}{Colors.RESET}\n")

    # Basic GET/POST/HEAD tests
    print(f"\n{Colors.YELLOW}=== Basic Tests ==={Colors.RESET}")
    test_get('/')
    test_post('/', 0)
    test_head('/')

    # Directory tests
    print(f"\n{Colors.YELLOW}=== Directory Tests ==={Colors.RESET}")
    test_get('/directory')
    test_get('/directory/youpi.bad_extension')
    test_get('/directory/youpi.bla')
    test_get('/directory/oulalala', expect_404=True)
    test_get('/directory/nop')
    test_get('/directory/nop/')
    test_get('/directory/nop/other.pouic')
    test_get('/directory/nop/other.pouac', expect_404=True)
    test_get('/directory/Yeah', expect_404=True)
    test_get('/directory/Yeah/not_happy.bad_extension')

    # PUT tests
    print(f"\n{Colors.YELLOW}=== PUT Tests ==={Colors.RESET}")
    test_put('/put_test/file_should_exist_after', 1000)
    test_put('/put_test/file_should_exist_after', 10000000)

    # Large POST tests
    print(f"\n{Colors.YELLOW}=== Large POST Tests ==={Colors.RESET}")
    test_post('/directory/youpi.bla', 100000000, use_chunked=True)
    test_post('/directory/youpla.bla', 100000000, expect_status=404, use_chunked=True)
    test_post('/directory/youpi.bla', 100000, special_headers={'X-Special': 'test'})

    # POST body size tests
    print(f"\n{Colors.YELLOW}=== POST Body Size Tests ==={Colors.RESET}")
    test_post('/post_body', 0)
    test_post('/post_body', 100)
    test_post('/post_body', 200, expect_status=413)  # Should exceed max body size of 100
    test_post('/post_body', 101, expect_status=413)  # Should exceed max body size of 100

    # Concurrent tests
    print(f"\n{Colors.YELLOW}=== Concurrent Tests ==={Colors.RESET}")
    test_concurrent("Multiple workers(5) x 15: GET /", worker_get, 5, 15, '/')
    test_concurrent("Multiple workers(20) x 5000: GET /", worker_get, 20, 5000, '/')
    test_concurrent("Multiple workers(128) x 50: GET /directory/nop", worker_get, 128, 50, '/directory/nop')
    test_concurrent("Multiple workers(20) x 5: PUT /put_test/multiple_same size=1000000",
                   worker_put, 20, 5, '/put_test/multiple_same', 1000000)

    # Summary
    print(f"\n{Colors.BLUE}{'='*70}{Colors.RESET}")
    print(f"{Colors.BLUE}Test Summary{Colors.RESET}")
    print(f"{Colors.BLUE}{'='*70}{Colors.RESET}")
    print(f"Total tests: {test_count}")
    print(f"{Colors.GREEN}Passed: {passed_count}{Colors.RESET}")
    print(f"{Colors.RED}Failed: {failed_count}{Colors.RESET}")

    if failed_count == 0:
        print(f"\n{Colors.GREEN}All tests passed!{Colors.RESET}\n")
        return 0
    else:
        print(f"\n{Colors.RED}Some tests failed!{Colors.RESET}\n")
        return 1

if __name__ == '__main__':
    sys.exit(main())
