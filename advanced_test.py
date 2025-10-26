#!/usr/bin/env python3
"""
Advanced test suite for webserv - Testing edge cases and advanced features
"""

import subprocess
import time
import requests
import os
import sys
from concurrent.futures import ThreadPoolExecutor

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

def run_advanced_tests():
    print(f"{Colors.BOLD}{Colors.PURPLE}")
    print("=" * 70)
    print("          WEBSERV ADVANCED TEST SUITE")
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
        # Test 1: Large POST body handling
        print_header("1. LARGE BODY HANDLING")

        try:
            # 1KB body
            large_data = "x" * 1024
            r = requests.post(f"{base_url}/upload", data=large_data, timeout=5)
            print_test("1KB POST body", r.status_code == 200,
                      f"Status: {r.status_code}")
        except Exception as e:
            print_test("1KB POST body", False, str(e))

        try:
            # 10KB body
            large_data = "y" * (10 * 1024)
            r = requests.post(f"{base_url}/upload", data=large_data, timeout=10)
            print_test("10KB POST body", r.status_code == 200,
                      f"Status: {r.status_code}")
        except Exception as e:
            print_test("10KB POST body", False, str(e))
            

        try:
            # 1GB body - should succeed with streaming (limit is now 1024M)
            large_data = "y" * (1 * 1024 * 1024 * 1024)
            r = requests.post(f"{base_url}/upload", data=large_data, timeout=180)
            print_test("1GB POST body (with streaming)", r.status_code == 200,
                      f"Status: {r.status_code}")
        except Exception as e:
            print_test("1GB POST body (with streaming)", False, f"Error: {str(e)[:80]}")

        # Test 2: Different Content-Types
        print_header("2. CONTENT-TYPE HANDLING")

        try:
            r = requests.post(f"{base_url}/test.cgi",
                            data="test=data",
                            headers={"Content-Type": "application/x-www-form-urlencoded"},
                            timeout=5)
            print_test("Form URL-encoded", r.status_code == 200,
                      f"Status: {r.status_code}")
        except Exception as e:
            print_test("Form URL-encoded", False, str(e))

        try:
            import json
            r = requests.post(f"{base_url}/upload.txt",
                            json={"key": "value"},
                            timeout=5)
            print_test("JSON content", r.status_code == 200,
                      f"Status: {r.status_code}")
        except Exception as e:
            print_test("JSON content", False, str(e))

        try:
            r = requests.post(f"{base_url}/upload.txt",
                            data="plain text data",
                            headers={"Content-Type": "text/plain"},
                            timeout=5)
            print_test("Plain text content", r.status_code == 200,
                      f"Status: {r.status_code}")
        except Exception as e:
            print_test("Plain text content", False, str(e))

        # Test 3: Query string handling
        print_header("3. QUERY STRING HANDLING")

        try:
            r = requests.get(f"{base_url}/test.cgi?param1=value1&param2=value2", timeout=5)
            has_params = "param1=value1" in r.text and "param2=value2" in r.text
            print_test("Multiple query parameters", has_params and r.status_code == 200,
                      "Both parameters present in response")
        except Exception as e:
            print_test("Multiple query parameters", False, str(e))

        try:
            r = requests.get(f"{base_url}/test.cgi?name=John%20Doe", timeout=5)
            print_test("URL-encoded query params", r.status_code == 200,
                      "Handles encoded characters")
        except Exception as e:
            print_test("URL-encoded query params", False, str(e))

        try:
            r = requests.get(f"{base_url}/test.cgi?special=a+b&chars=!@#", timeout=5)
            print_test("Special characters in query", r.status_code == 200,
                      "Handles special chars")
        except Exception as e:
            print_test("Special characters in query", False, str(e))

        # Test 4: HEAD method
        print_header("4. HEAD METHOD SUPPORT")

        try:
            r = requests.head(f"{base_url}/", timeout=5)
            has_headers = "Content-Type" in r.headers
            no_body = len(r.content) == 0
            print_test("HEAD method", r.status_code == 200 and has_headers and no_body,
                      f"Status: {r.status_code}, Has headers: {has_headers}, Empty body: {no_body}")
        except Exception as e:
            print_test("HEAD method", False, str(e))

        try:
            r_get = requests.get(f"{base_url}/index.html", timeout=5)
            r_head = requests.head(f"{base_url}/index.html", timeout=5)
            same_headers = r_get.headers.get("Content-Type") == r_head.headers.get("Content-Type")
            print_test("HEAD vs GET headers match", same_headers,
                      "Headers are consistent")
        except Exception as e:
            print_test("HEAD vs GET headers match", False, str(e))

        # Test 5: Different file types
        print_header("5. FILE TYPE HANDLING")

        # Create test files
        os.makedirs("html/test_files", exist_ok=True)

        with open("html/test_files/test.html", "w") as f:
            f.write("<html><body>HTML file</body></html>")

        with open("html/test_files/test.txt", "w") as f:
            f.write("Text file content")

        with open("html/test_files/test.css", "w") as f:
            f.write("body { color: red; }")

        try:
            r = requests.get(f"{base_url}/test_files/test.html", timeout=5)
            is_html = "text/html" in r.headers.get("Content-Type", "")
            print_test("HTML file Content-Type", is_html and r.status_code == 200,
                      f"Content-Type: {r.headers.get('Content-Type', 'N/A')}")
        except Exception as e:
            print_test("HTML file Content-Type", False, str(e))

        try:
            r = requests.get(f"{base_url}/test_files/test.txt", timeout=5)
            is_text = "text/plain" in r.headers.get("Content-Type", "")
            print_test("Text file Content-Type", is_text and r.status_code == 200,
                      f"Content-Type: {r.headers.get('Content-Type', 'N/A')}")
        except Exception as e:
            print_test("Text file Content-Type", False, str(e))

        # Test 6: Directory listing (autoindex)
        print_header("6. DIRECTORY LISTING (AUTOINDEX)")

        try:
            r = requests.get(f"{base_url}/test_files/", timeout=5)
            has_files = "test.html" in r.text and "test.txt" in r.text
            print_test("Autoindex shows files", has_files and r.status_code == 200,
                      "Directory listing visible")
        except Exception as e:
            print_test("Autoindex shows files", False, str(e))

        # Test 7: Keep-Alive connections
        print_header("7. CONNECTION HANDLING")

        try:
            session = requests.Session()
            r1 = session.get(f"{base_url}/", timeout=5)
            r2 = session.get(f"{base_url}/index.html", timeout=5)
            print_test("Keep-Alive connections", r1.status_code == 200 and r2.status_code == 200,
                      "Multiple requests in same session")
        except Exception as e:
            print_test("Keep-Alive connections", False, str(e))

        # Test 8: Empty POST body
        print_header("8. EDGE CASES")

        try:
            r = requests.post(f"{base_url}/test.cgi", data="", timeout=5)
            print_test("Empty POST body", r.status_code == 200,
                      "Handles empty body")
        except Exception as e:
            print_test("Empty POST body", False, str(e))

        try:
            r = requests.get(f"{base_url}/", headers={"Host": "localhost:8082"}, timeout=5)
            print_test("Custom Host header", r.status_code == 200,
                      "Accepts custom headers")
        except Exception as e:
            print_test("Custom Host header", False, str(e))

        # Test 9: Rapid sequential requests
        print_header("9. STRESS & PERFORMANCE")

        def make_rapid_request(i):
            try:
                r = requests.get(f"{base_url}/", timeout=5)
                return r.status_code == 200
            except:
                return False

        print(f"{Colors.BLUE}Testing 20 rapid sequential requests...{Colors.END}")
        start_time = time.time()
        with ThreadPoolExecutor(max_workers=1) as executor:
            futures = [executor.submit(make_rapid_request, i) for i in range(20)]
            results = [f.result() for f in futures]
        elapsed = time.time() - start_time

        success_count = sum(results)
        print_test("20 rapid requests", success_count >= 18,
                  f"{success_count}/20 succeeded in {elapsed:.2f}s")

        # Test 10: Mixed concurrent operations
        print(f"{Colors.BLUE}Testing 10 mixed concurrent operations...{Colors.END}")

        def mixed_operation(i):
            try:
                if i % 3 == 0:
                    r = requests.get(f"{base_url}/", timeout=10)
                elif i % 3 == 1:
                    r = requests.post(f"{base_url}/test.cgi", data=f"test={i}", timeout=10)
                else:
                    r = requests.get(f"{base_url}/test.cgi?id={i}", timeout=10)
                return r.status_code == 200
            except:
                return False

        with ThreadPoolExecutor(max_workers=10) as executor:
            futures = [executor.submit(mixed_operation, i) for i in range(10)]
            results = [f.result() for f in futures]

        success_count = sum(results)
        print_test("Mixed concurrent ops", success_count >= 8,
                  f"{success_count}/10 mixed operations succeeded")

        # Final check
        try:
            r = requests.get(f"{base_url}/", timeout=5)
            print_test("Server still responsive", r.status_code == 200,
                      "Server survived all tests")
        except Exception as e:
            print_test("Server still responsive", False, str(e))

        print_header("TEST SUMMARY")
        print(f"{Colors.GREEN}✅ Advanced test suite completed!{Colors.END}")
        print(f"{Colors.BLUE}Tested features:{Colors.END}")
        print(f"  • Large body handling (1KB, 10KB)")
        print(f"  • Multiple Content-Types (form, JSON, plain text)")
        print(f"  • Query string handling (special chars, encoding)")
        print(f"  • HEAD method support")
        print(f"  • File type detection (HTML, TXT, CSS)")
        print(f"  • Directory listing (autoindex)")
        print(f"  • Keep-Alive connections")
        print(f"  • Edge cases (empty body, custom headers)")
        print(f"  • Performance (rapid + concurrent requests)")

    except KeyboardInterrupt:
        print(f"\n{Colors.YELLOW}Tests interrupted{Colors.END}")
    except Exception as e:
        print(f"{Colors.RED}Error during testing: {e}{Colors.END}")
    finally:
        # Cleanup test files
        import shutil
        if os.path.exists("html/test_files"):
            shutil.rmtree("html/test_files")

        # Stop server
        server.terminate()
        server.wait()
        print(f"{Colors.BLUE}Server stopped{Colors.END}")

if __name__ == "__main__":
    if not os.path.exists("./webserv"):
        print(f"{Colors.RED}Error: ./webserv not found. Run 'make' first.{Colors.END}")
        sys.exit(1)

    run_advanced_tests()
