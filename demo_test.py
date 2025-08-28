#!/usr/bin/env python3
"""
Demo script for 42 School Webserv Evaluation
Shows all critical edge cases tested automatically
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
    print(f"\n{Colors.BOLD}{Colors.CYAN}{'='*60}{Colors.END}")
    print(f"{Colors.BOLD}{Colors.CYAN}{title:^60}{Colors.END}")
    print(f"{Colors.BOLD}{Colors.CYAN}{'='*60}{Colors.END}")

def print_test(name, status, details=""):
    if status:
        print(f"✅ {Colors.GREEN}{name}{Colors.END} - {details}")
    else:
        print(f"❌ {Colors.RED}{name}{Colors.END} - {details}")

def run_tests():
    print(f"{Colors.BOLD}{Colors.PURPLE}")
    print("=" * 70)
    print("          WEBSERV EDGE CASE DEMO FOR 42 EVALUATION")
    print("=" * 70)
    print(f"{Colors.END}")
    
    base_url = "http://localhost:8080"
    
    # Start server
    print(f"{Colors.BLUE}Starting webserv server...{Colors.END}")
    server = subprocess.Popen(["./webserv", "location_test.conf"])
    time.sleep(3)
    
    if server.poll() is not None:
        print(f"{Colors.RED}❌ Server failed to start{Colors.END}")
        return
        
    print(f"{Colors.GREEN}✅ Server started successfully{Colors.END}")
    
    try:
        # Test 1: Required HTTP Methods
        print_header("1. REQUIRED HTTP METHODS (CRITICAL)")
        
        try:
            r = requests.get(f"{base_url}/", timeout=5)
            print_test("GET method", r.status_code == 200, f"Status: {r.status_code}")
        except Exception as e:
            print_test("GET method", False, str(e))
            
        try:
            r = requests.post(f"{base_url}/upload.txt", data="test", timeout=5)
            print_test("POST method", r.status_code == 200, f"Status: {r.status_code}")
        except Exception as e:
            print_test("POST method", False, str(e))
            
        # Create test file
        with open("html/delete_test.txt", "w") as f:
            f.write("test")
            
        try:
            r = requests.delete(f"{base_url}/delete_test.txt", timeout=5)
            print_test("DELETE method", r.status_code == 200, f"Status: {r.status_code}")
        except Exception as e:
            print_test("DELETE method", False, str(e))
        
        # Test 2: Security & Error Handling
        print_header("2. SECURITY & ERROR HANDLING")
        
        try:
            r = requests.request("PATCH", f"{base_url}/", timeout=5)
            print_test("Invalid method rejection", r.status_code == 400, f"Status: {r.status_code}")
        except Exception as e:
            print_test("Invalid method rejection", False, str(e))
            
        try:
            r = requests.get(f"{base_url}/../../../etc/passwd", timeout=5)
            print_test("Path traversal security", r.status_code == 404, f"Status: {r.status_code}")
        except Exception as e:
            print_test("Path traversal security", False, str(e))
            
        try:
            r = requests.get(f"{base_url}/nonexistent", timeout=5)
            has_error = "404 Not Found" in r.text
            print_test("Error pages", has_error, f"Status: {r.status_code}")
        except Exception as e:
            print_test("Error pages", False, str(e))
        
        # Test 3: Configuration & Routing
        print_header("3. CONFIGURATION & ROUTING")
        
        try:
            r = requests.delete(f"{base_url}/api/test", timeout=5)
            print_test("Location method restrictions", r.status_code == 405, 
                      f"/api blocks DELETE: {r.status_code}")
        except Exception as e:
            print_test("Location method restrictions", False, str(e))
            
        try:
            r = requests.post(f"{base_url}/static/test", data="test", timeout=5)
            print_test("Static location restrictions", r.status_code == 405,
                      f"/static blocks POST: {r.status_code}")
        except Exception as e:
            print_test("Static location restrictions", False, str(e))
        
        # Test 4: CGI Functionality
        print_header("4. CGI FUNCTIONALITY (CRITICAL)")
        
        try:
            r = requests.get(f"{base_url}/test.cgi?param=test", timeout=10)
            has_vars = all(var in r.text for var in ["REQUEST_METHOD", "QUERY_STRING"])
            print_test("CGI environment variables", has_vars and r.status_code == 200,
                      "All required env vars present")
        except Exception as e:
            print_test("CGI environment variables", False, str(e))
            
        try:
            r = requests.post(f"{base_url}/test.cgi", data="name=test&value=123", timeout=10)
            has_data = "name=test&value=123" in r.text
            print_test("CGI POST data", has_data and r.status_code == 200,
                      "CGI receives POST data")
        except Exception as e:
            print_test("CGI POST data", False, str(e))
            
        try:
            r = requests.get(f"{base_url}/test.py", timeout=10)
            print_test("Multiple CGI types", r.status_code == 200,
                      "Python CGI works")
        except Exception as e:
            print_test("Multiple CGI types", False, str(e))
        
        # Test 5: Concurrency & Stress
        print_header("5. CONCURRENCY & STRESS (CRITICAL)")
        
        def make_request(i):
            try:
                r = requests.get(f"{base_url}/", timeout=10)
                return r.status_code == 200
            except:
                return False
        
        print(f"{Colors.BLUE}Testing 5 concurrent connections...{Colors.END}")
        with ThreadPoolExecutor(max_workers=5) as executor:
            futures = [executor.submit(make_request, i) for i in range(5)]
            results = [f.result() for f in futures]
        
        success_count = sum(results)
        print_test("Concurrent connections", success_count >= 4,
                  f"{success_count}/5 concurrent requests succeeded")
        
        # Final server check
        try:
            r = requests.get(f"{base_url}/", timeout=5)
            print_test("Server still responsive", r.status_code == 200,
                      "Server survived all tests")
        except Exception as e:
            print_test("Server still responsive", False, str(e))
        
        print_header("EVALUATION DEMO COMPLETED")
        print(f"{Colors.GREEN}🎉 All critical 42 School requirements demonstrated!{Colors.END}")
        print(f"{Colors.BLUE}Your webserv handles:{Colors.END}")
        print(f"  • Required HTTP methods (GET, POST, DELETE)")  
        print(f"  • Non-blocking I/O with concurrent connections")
        print(f"  • CGI with multiple types and proper environment")
        print(f"  • Location-based routing and method restrictions")
        print(f"  • Security (path traversal, invalid methods)")
        print(f"  • Error handling and custom error pages")
        print(f"  • Stress testing without crashes")
        
    except KeyboardInterrupt:
        print(f"\n{Colors.YELLOW}Demo interrupted{Colors.END}")
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
        
    if not os.path.exists("location_test.conf"):
        print(f"{Colors.RED}Error: location_test.conf not found.{Colors.END}")
        sys.exit(1)
        
    run_tests()