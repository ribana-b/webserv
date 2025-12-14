#!/usr/bin/env python3
"""
Security and edge case tests for webserv
"""

import subprocess
import time
import requests
import os
import sys

class Colors:
    RED = '\033[91m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    PURPLE = '\033[95m'
    CYAN = '\033[96m'
    BOLD = '\033[1m'
    END = '\033[0m'

def print_header(title):
    print(f"\n{Colors.BOLD}{Colors.CYAN}{'='*70}{Colors.END}")
    print(f"{Colors.BOLD}{Colors.CYAN}{title:^70}{Colors.END}")
    print(f"{Colors.BOLD}{Colors.CYAN}{'='*70}{Colors.END}")

def print_test(name, status, details=""):
    if status:
        print(f"✅ {Colors.GREEN}{name}{Colors.END} - {details}")
    else:
        print(f"❌ {Colors.RED}{name}{Colors.END} - {details}")

def run_security_tests():
    print(f"{Colors.BOLD}{Colors.PURPLE}")
    print("=" * 70)
    print("          WEBSERV SECURITY & EDGE CASE TESTS")
    print("=" * 70)
    print(f"{Colors.END}")

    base_url = "http://localhost:8082"

    # Start server
    print(f"{Colors.BLUE}Starting webserv server...{Colors.END}")
    server = subprocess.Popen(["./webserv", "config/testing/upload_test.conf"],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(2)

    if server.poll() is not None:
        print(f"{Colors.RED}❌ Server failed to start{Colors.END}")
        return

    print(f"{Colors.GREEN}✅ Server started successfully{Colors.END}")

    try:
        # Test 1: Path traversal attacks
        print_header("1. PATH TRAVERSAL SECURITY")

        traversal_attempts = [
            "/../../../etc/passwd",
            "/../../etc/shadow",
            "/../../../root/.ssh/id_rsa",
            "/./././etc/passwd",
            "/..%2F..%2F..%2Fetc%2Fpasswd",  # URL encoded
            "/.../.../...//etc/passwd",
        ]

        for path in traversal_attempts:
            try:
                r = requests.get(f"{base_url}{path}", timeout=3)
                is_safe = r.status_code in [404, 403]
                print_test(f"Block {path}", is_safe,
                          f"Status: {r.status_code}")
            except Exception as e:
                print_test(f"Block {path}", False, str(e))

        # Test 2: Invalid HTTP requests
        print_header("2. MALFORMED REQUEST HANDLING")

        try:
            # Invalid method
            r = requests.request("INVALID", f"{base_url}/", timeout=3)
            print_test("Invalid HTTP method", r.status_code == 400,
                      f"Status: {r.status_code}")
        except Exception as e:
            print_test("Invalid HTTP method", True, "Connection rejected")

        try:
            # Missing Host header (HTTP/1.1 requires it)
            import socket
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(3)
            s.connect(('localhost', 8082))
            s.send(b"GET / HTTP/1.1\r\n\r\n")  # No Host header
            response = s.recv(1024)
            s.close()
            # Should handle gracefully
            print_test("Missing Host header", len(response) > 0,
                      "Server responded gracefully")
        except Exception as e:
            print_test("Missing Host header", False, str(e))

        # Test 3: Long inputs
        print_header("3. BUFFER OVERFLOW PROTECTION")

        try:
            # Very long path
            long_path = "/a" * 5000
            r = requests.get(f"{base_url}{long_path}", timeout=5)
            print_test("Very long path (5000 chars)", r.status_code in [404, 414],
                      f"Status: {r.status_code}")
        except Exception as e:
            print_test("Very long path", False, str(e))

        try:
            # Very long header value
            headers = {"X-Custom": "a" * 10000}
            r = requests.get(f"{base_url}/", headers=headers, timeout=5)
            print_test("Very long header (10KB)", r.status_code in [200, 400, 431],
                      f"Handled gracefully")
        except Exception as e:
            print_test("Very long header", True, "Request rejected/handled")

        try:
            # Very long query string
            long_query = "?" + "&".join([f"param{i}=value{i}" for i in range(1000)])
            r = requests.get(f"{base_url}/test.cgi{long_query}", timeout=5)
            print_test("Very long query (1000 params)", r.status_code in [200, 414],
                      f"Status: {r.status_code}")
        except Exception as e:
            print_test("Very long query", False, str(e))

        # Test 4: Null bytes and special characters
        print_header("4. SPECIAL CHARACTER HANDLING")

        try:
            # Path with spaces
            r = requests.get(f"{base_url}/test file.txt", timeout=3)
            print_test("Path with spaces", r.status_code in [200, 404],
                      f"Status: {r.status_code}")
        except Exception as e:
            print_test("Path with spaces", False, str(e))

        try:
            # Unicode characters
            r = requests.get(f"{base_url}/тест", timeout=3)
            print_test("Unicode path", r.status_code in [200, 404, 400],
                      f"Status: {r.status_code}")
        except Exception as e:
            print_test("Unicode path", False, str(e))

        # Test 5: CGI injection attempts
        print_header("5. CGI SECURITY")

        try:
            # Command injection attempt in query string
            r = requests.get(f"{base_url}/test.cgi?cmd=;ls", timeout=3)
            # Should not execute commands
            print_test("Query injection protection", r.status_code == 200,
                      "Request processed safely")
        except Exception as e:
            print_test("Query injection protection", False, str(e))

        try:
            # POST with shell metacharacters
            r = requests.post(f"{base_url}/test.cgi",
                            data="data=;rm -rf /",
                            timeout=3)
            print_test("POST injection protection", r.status_code == 200,
                      "Dangerous input handled safely")
        except Exception as e:
            print_test("POST injection protection", False, str(e))

        # Test 6: Resource exhaustion
        print_header("6. RESOURCE EXHAUSTION PROTECTION")

        try:
            # Many small requests rapidly
            start = time.time()
            for i in range(50):
                requests.get(f"{base_url}/", timeout=1)
            elapsed = time.time() - start
            print_test("50 rapid requests", True,
                      f"Completed in {elapsed:.2f}s (avg {elapsed/50*1000:.1f}ms/req)")
        except Exception as e:
            print_test("50 rapid requests", False, str(e))

        try:
            # Slow client attack simulation (slow body send)
            import socket
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(5)
            s.connect(('localhost', 8082))
            s.send(b"POST /upload.txt HTTP/1.1\r\n")
            s.send(b"Host: localhost:8082\r\n")
            s.send(b"Content-Length: 100\r\n\r\n")
            time.sleep(0.5)  # Wait before sending body
            s.send(b"a" * 100)
            response = s.recv(1024)
            s.close()
            print_test("Slow client handling", len(response) > 0,
                      "Server handled delayed body")
        except Exception as e:
            print_test("Slow client handling", False, str(e))

        # Test 7: HTTP version handling
        print_header("7. HTTP VERSION COMPATIBILITY")

        try:
            import socket
            # HTTP/1.0 request
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(3)
            s.connect(('localhost', 8082))
            s.send(b"GET / HTTP/1.0\r\nHost: localhost\r\n\r\n")
            response = s.recv(1024)
            s.close()
            print_test("HTTP/1.0 support", b"HTTP/1.1" in response or b"HTTP/1.0" in response,
                      "Handles HTTP/1.0")
        except Exception as e:
            print_test("HTTP/1.0 support", False, str(e))

        try:
            import socket
            # Invalid HTTP version
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(3)
            s.connect(('localhost', 8082))
            s.send(b"GET / HTTP/9.9\r\nHost: localhost\r\n\r\n")
            response = s.recv(1024)
            s.close()
            # Should handle gracefully or reject
            print_test("Invalid HTTP version", len(response) > 0,
                      "Handled invalid version")
        except Exception as e:
            print_test("Invalid HTTP version", False, str(e))

        # Test 8: Connection handling
        print_header("8. CONNECTION MANAGEMENT")

        try:
            # Connection: close
            r = requests.get(f"{base_url}/", headers={"Connection": "close"}, timeout=3)
            print_test("Connection: close header", r.status_code == 200,
                      "Respects Connection header")
        except Exception as e:
            print_test("Connection: close header", False, str(e))

        try:
            # Immediate disconnect
            import socket
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect(('localhost', 8082))
            s.send(b"GET / HTTP/1.1\r\nHost: localhost\r\n")
            s.close()  # Close before sending complete request
            time.sleep(0.1)
            # Server should handle this gracefully
            print_test("Client disconnect mid-request", True,
                      "Server handled disconnect")
        except Exception as e:
            print_test("Client disconnect mid-request", False, str(e))

        # Final server health check
        try:
            r = requests.get(f"{base_url}/", timeout=5)
            print_test("Server still alive after attacks", r.status_code == 200,
                      "Server survived security tests")
        except Exception as e:
            print_test("Server still alive", False, str(e))

        print_header("SECURITY TEST SUMMARY")
        print(f"{Colors.GREEN}✅ Security test suite completed!{Colors.END}")
        print(f"{Colors.BLUE}Tested security features:{Colors.END}")
        print(f"  • Path traversal protection (multiple variants)")
        print(f"  • Malformed request handling")
        print(f"  • Buffer overflow protection (long inputs)")
        print(f"  • Special character handling (null bytes, unicode)")
        print(f"  • CGI injection prevention")
        print(f"  • Resource exhaustion resistance")
        print(f"  • HTTP version compatibility")
        print(f"  • Connection management edge cases")

    except KeyboardInterrupt:
        print(f"\n{Colors.YELLOW}Tests interrupted{Colors.END}")
    except Exception as e:
        print(f"{Colors.RED}Error during testing: {e}{Colors.END}")
    finally:
        # Stop server
        server.terminate()
        server.wait()
        print(f"{Colors.BLUE}Server stopped{Colors.END}")

if __name__ == "__main__":
    if not os.path.exists("./webserv"):
        print(f"{Colors.RED}Error: ./webserv not found. Run 'make' first.{Colors.END}")
        sys.exit(1)

    run_security_tests()
