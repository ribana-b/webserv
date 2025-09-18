#!/usr/bin/env python3
"""
Comprehensive Webserv Testing Script
Tests all critical edge cases according to 42 School subject requirements.
Usage: python3 test_webserv.py [config_file] [port]
"""

import subprocess
import time
import requests
import os
import sys
import signal
from concurrent.futures import ThreadPoolExecutor
import tempfile

class Colors:
    """ANSI color codes for terminal output"""
    RED = '\033[91m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    PURPLE = '\033[95m'
    CYAN = '\033[96m'
    WHITE = '\033[97m'
    BOLD = '\033[1m'
    END = '\033[0m'

class WebservTester:
    def __init__(self, config_file="location_test.conf", port=8080):
        self.config_file = config_file
        self.port = port
        self.base_url = f"http://localhost:{port}"
        self.server_process = None
        self.passed_tests = 0
        self.total_tests = 0
        
    def print_header(self, title):
        """Print a formatted section header"""
        print(f"\n{Colors.BOLD}{Colors.CYAN}{'='*60}{Colors.END}")
        print(f"{Colors.BOLD}{Colors.CYAN}{title:^60}{Colors.END}")
        print(f"{Colors.BOLD}{Colors.CYAN}{'='*60}{Colors.END}")
        
    def print_test(self, test_name, status, details="", critical=False):
        """Print test result with color coding"""
        self.total_tests += 1
        marker = f"{Colors.BOLD}{Colors.RED}[CRITICAL]{Colors.END} " if critical else ""
        
        if status:
            self.passed_tests += 1
            print(f"{marker}✅ {Colors.GREEN}{test_name}{Colors.END} - {details}")
        else:
            print(f"{marker}❌ {Colors.RED}{test_name}{Colors.END} - {details}")
            
    def print_warning(self, test_name, details=""):
        """Print warning for potential issues"""
        self.total_tests += 1
        print(f"⚠️  {Colors.YELLOW}{test_name}{Colors.END} - {details}")
        
    def start_server(self):
        """Start webserv server in background"""
        print(f"{Colors.BLUE}Starting webserv server...{Colors.END}")
        try:
            # Kill any existing webserv processes
            subprocess.run(["pkill", "-f", "webserv"], capture_output=True, timeout=5)
            time.sleep(1)
            
            # Start server with proper process handling
            self.server_process = subprocess.Popen(
                ["./webserv", self.config_file],
                preexec_fn=os.setsid  # Create new process group
            )
            
            # Wait longer for server to start
            time.sleep(3)
            
            # Check if process is still running
            if self.server_process.poll() is not None:
                print(f"{Colors.RED}Server process terminated immediately{Colors.END}")
                return False
            
            # Test if server is responding
            for attempt in range(5):
                try:
                    response = requests.get(f"{self.base_url}/", timeout=2)
                    print(f"{Colors.GREEN}Server started successfully on port {self.port}{Colors.END}")
                    return True
                except requests.exceptions.RequestException:
                    if attempt < 4:  # Don't sleep on last attempt
                        time.sleep(1)
                    continue
            
            print(f"{Colors.RED}Server not responding after 5 attempts{Colors.END}")
            return False
                
        except Exception as e:
            print(f"{Colors.RED}Error starting server: {e}{Colors.END}")
            return False
            
    def stop_server(self):
        """Stop webserv server"""
        if self.server_process:
            try:
                # Kill the process group to ensure all child processes are terminated
                os.killpg(os.getpgid(self.server_process.pid), signal.SIGTERM)
                self.server_process.wait(timeout=5)
            except (ProcessLookupError, OSError):
                # Process already terminated
                pass
            except subprocess.TimeoutExpired:
                # Force kill if it doesn't terminate gracefully
                try:
                    os.killpg(os.getpgid(self.server_process.pid), signal.SIGKILL)
                except (ProcessLookupError, OSError):
                    pass
            print(f"{Colors.BLUE}Server stopped{Colors.END}")
            
    def test_http_protocol_edge_cases(self):
        """Test HTTP protocol edge cases - CRITICAL for evaluation"""
        self.print_header("1. HTTP PROTOCOL EDGE CASES (CRITICAL)")
        
        # Test required HTTP methods (subject requirement)
        try:
            response = requests.get(f"{self.base_url}/", timeout=5)
            self.print_test("GET method required", response.status_code == 200, 
                          f"Status: {response.status_code}", critical=True)
        except Exception as e:
            self.print_test("GET method required", False, f"Error: {e}", critical=True)
            
        try:
            response = requests.post(f"{self.base_url}/upload.txt", data="test", timeout=5)
            self.print_test("POST method required", response.status_code == 200, 
                          f"Status: {response.status_code}", critical=True)
        except Exception as e:
            self.print_test("POST method required", False, f"Error: {e}", critical=True)
            
        # Create test file for DELETE
        with open("html/test_delete.txt", "w") as f:
            f.write("test file")
            
        try:
            response = requests.delete(f"{self.base_url}/test_delete.txt", timeout=5)
            self.print_test("DELETE method required", response.status_code == 200, 
                          f"Status: {response.status_code}", critical=True)
        except Exception as e:
            self.print_test("DELETE method required", False, f"Error: {e}", critical=True)
            
        # Test invalid HTTP method (should return 400)
        try:
            response = requests.request("PATCH", f"{self.base_url}/", timeout=5)
            self.print_test("Invalid method rejection", response.status_code == 400, 
                          f"Status: {response.status_code}", critical=True)
        except Exception as e:
            self.print_test("Invalid method rejection", False, f"Error: {e}", critical=True)
            
        # Test large headers (should not crash server)
        try:
            large_header = "A" * 8192
            response = requests.get(f"{self.base_url}/", 
                                 headers={"X-Large-Header": large_header}, timeout=5)
            self.print_test("Large headers handling", response.status_code == 200, 
                          "Server handles large headers without crash")
        except Exception as e:
            self.print_test("Large headers handling", False, f"Error: {e}")
            
        # Test path traversal security (should return 404, not system files)
        try:
            response = requests.get(f"{self.base_url}/../../../etc/passwd", timeout=5)
            self.print_test("Path traversal security", response.status_code == 404, 
                          f"Status: {response.status_code} (blocked correctly)", critical=True)
        except Exception as e:
            self.print_test("Path traversal security", False, f"Error: {e}", critical=True)
            
    def test_nonblocking_io_cases(self):
        """Test non-blocking I/O critical cases - CRITICAL (failure = 0 points)"""
        self.print_header("2. NON-BLOCKING I/O CASES (CRITICAL - FAILURE = 0)")
        
        # Test concurrent connections (subject requirement)
        def make_request(i):
            try:
                response = requests.get(f"{self.base_url}/", timeout=10)
                return response.status_code == 200
            except:
                return False
                
        try:
            with ThreadPoolExecutor(max_workers=5) as executor:
                futures = [executor.submit(make_request, i) for i in range(5)]
                results = [f.result() for f in futures]
                
            success_count = sum(results)
            self.print_test("Concurrent connections", success_count >= 3, 
                          f"{success_count}/5 concurrent requests succeeded", critical=True)
        except Exception as e:
            self.print_test("Concurrent connections", False, f"Error: {e}", critical=True)
            
        # Test server doesn't hang indefinitely
        try:
            response = requests.get(f"{self.base_url}/", timeout=5)
            self.print_test("No indefinite hang", True, 
                          "Server responds within timeout", critical=True)
        except requests.exceptions.Timeout:
            self.print_test("No indefinite hang", False, 
                          "Server hangs indefinitely", critical=True)
        except Exception as e:
            self.print_test("No indefinite hang", False, f"Error: {e}", critical=True)
            
    def test_configuration_edge_cases(self):
        """Test configuration edge cases"""
        self.print_header("3. CONFIGURATION EDGE CASES")
        
        # Test location-based method restrictions
        try:
            response = requests.delete(f"{self.base_url}/api/test", timeout=5)
            self.print_test("Location method restrictions", response.status_code == 405, 
                          f"DELETE blocked in /api location: {response.status_code}")
        except Exception as e:
            self.print_test("Location method restrictions", False, f"Error: {e}")
            
        try:
            response = requests.post(f"{self.base_url}/static/test", data="test", timeout=5)
            self.print_test("Static location restrictions", response.status_code == 405, 
                          f"POST blocked in /static location: {response.status_code}")
        except Exception as e:
            self.print_test("Static location restrictions", False, f"Error: {e}")
            
        # Test error pages (subject requirement)
        try:
            response = requests.get(f"{self.base_url}/nonexistent", timeout=5)
            has_error_page = "404 Not Found" in response.text
            self.print_test("Default error pages", has_error_page and response.status_code == 404,
                          f"Status: {response.status_code}, Has error page: {has_error_page}", critical=True)
        except Exception as e:
            self.print_test("Default error pages", False, f"Error: {e}", critical=True)
            
    def test_cgi_critical_cases(self):
        """Test CGI critical cases (subject pages 11-12)"""
        self.print_header("4. CGI CRITICAL CASES")
        
        # Test CGI environment variables (subject requirement)
        try:
            response = requests.get(f"{self.base_url}/test.cgi?param=test", timeout=10)
            has_env_vars = all(var in response.text for var in 
                             ["REQUEST_METHOD", "QUERY_STRING", "SCRIPT_NAME"])
            self.print_test("CGI environment variables", has_env_vars and response.status_code == 200,
                          f"All required env vars present", critical=True)
        except Exception as e:
            self.print_test("CGI environment variables", False, f"Error: {e}", critical=True)
            
        # Test CGI POST data
        try:
            response = requests.post(f"{self.base_url}/test.cgi", 
                                   data="name=test&value=123", timeout=10)
            has_post_data = "name=test&value=123" in response.text
            self.print_test("CGI POST data", has_post_data and response.status_code == 200,
                          "CGI receives POST data correctly", critical=True)
        except Exception as e:
            self.print_test("CGI POST data", False, f"Error: {e}", critical=True)
            
        # Test multiple CGI types (bonus)
        try:
            response = requests.get(f"{self.base_url}/test.py", timeout=10)
            self.print_test("Multiple CGI types", response.status_code == 200,
                          "Python CGI works (bonus feature)")
        except Exception as e:
            self.print_test("Multiple CGI types", False, f"Error: {e}")
            
    def test_filesystem_edge_cases(self):
        """Test file system edge cases"""
        self.print_header("5. FILE SYSTEM EDGE CASES")
        
        # Test file permissions
        test_file = "html/no_permission.txt"
        try:
            with open(test_file, "w") as f:
                f.write("secret")
            os.chmod(test_file, 0o000)  # No permissions
            
            response = requests.get(f"{self.base_url}/no_permission.txt", timeout=5)
            self.print_test("File permissions", response.status_code == 403,
                          f"No-permission file: {response.status_code}")
            
            # Cleanup
            os.chmod(test_file, 0o644)
            os.remove(test_file)
        except Exception as e:
            self.print_test("File permissions", False, f"Error: {e}")
            
        # Test large files
        large_file = "html/large_test.txt"
        try:
            with open(large_file, "w") as f:
                f.write("x" * 100000)  # 100KB file
                
            response = requests.get(f"{self.base_url}/large_test.txt", timeout=10)
            self.print_test("Large file serving", response.status_code == 200,
                          f"100KB file served: {response.status_code}")
            
            os.remove(large_file)
        except Exception as e:
            self.print_test("Large file serving", False, f"Error: {e}")
            
        # Test symlink handling (security consideration)
        symlink_file = "html/symlink_test"
        try:
            if os.path.exists("/etc/passwd"):
                os.symlink("/etc/passwd", symlink_file)
                response = requests.get(f"{self.base_url}/symlink_test", timeout=5)
                
                # Check if it returns system file content (potential security issue)
                if "root:" in response.text and response.status_code == 200:
                    self.print_warning("Symlink security", 
                                     "Server follows symlinks (potential security consideration)")
                else:
                    self.print_test("Symlink security", True, "Symlinks handled securely")
                    
                os.remove(symlink_file)
        except Exception as e:
            self.print_test("Symlink handling", False, f"Error: {e}")
            
    def test_resilience_and_stress(self):
        """Test resilience and stress (subject: 'remain operational at all times')"""
        self.print_header("6. RESILIENCE & STRESS (CRITICAL)")
        
        # Stress test with multiple concurrent requests
        def stress_request(i):
            try:
                response = requests.get(f"{self.base_url}/", timeout=10)
                return response.status_code == 200
            except:
                return False
                
        try:
            print(f"{Colors.BLUE}Running stress test with 10 concurrent requests...{Colors.END}")
            with ThreadPoolExecutor(max_workers=10) as executor:
                futures = [executor.submit(stress_request, i) for i in range(10)]
                results = [f.result() for f in futures]
                
            success_count = sum(results)
            
            # Test if server is still operational after stress
            time.sleep(1)
            post_stress = requests.get(f"{self.base_url}/", timeout=5)
            server_survived = post_stress.status_code == 200
            
            self.print_test("Stress test survival", server_survived and success_count >= 8,
                          f"Server survived stress test: {success_count}/10 requests succeeded", 
                          critical=True)
        except Exception as e:
            self.print_test("Stress test survival", False, f"Error: {e}", critical=True)
            
    def run_all_tests(self):
        """Run all edge case tests"""
        print(f"{Colors.BOLD}{Colors.PURPLE}")
        print("=" * 80)
        print("             WEBSERV COMPREHENSIVE EDGE CASE TESTING")
        print("                   42 School Evaluation Ready")
        print("=" * 80)
        print(f"{Colors.END}")
        
        if not self.start_server():
            print(f"{Colors.RED}Failed to start server. Exiting.{Colors.END}")
            return False
            
        try:
            self.test_http_protocol_edge_cases()
            self.test_nonblocking_io_cases()
            self.test_configuration_edge_cases()
            self.test_cgi_critical_cases()
            self.test_filesystem_edge_cases()
            self.test_resilience_and_stress()
            
        except KeyboardInterrupt:
            print(f"\n{Colors.YELLOW}Testing interrupted by user{Colors.END}")
        except Exception as e:
            print(f"\n{Colors.RED}Unexpected error during testing: {e}{Colors.END}")
        finally:
            self.stop_server()
            
        # Print final summary
        self.print_summary()
        
    def print_summary(self):
        """Print test summary"""
        print(f"\n{Colors.BOLD}{Colors.WHITE}{'='*60}{Colors.END}")
        print(f"{Colors.BOLD}{Colors.WHITE}                    TEST SUMMARY{Colors.END}")
        print(f"{Colors.BOLD}{Colors.WHITE}{'='*60}{Colors.END}")
        
        pass_rate = (self.passed_tests / self.total_tests * 100) if self.total_tests > 0 else 0
        
        print(f"Total Tests: {Colors.BOLD}{self.total_tests}{Colors.END}")
        print(f"Passed: {Colors.GREEN}{self.passed_tests}{Colors.END}")
        print(f"Failed: {Colors.RED}{self.total_tests - self.passed_tests}{Colors.END}")
        print(f"Pass Rate: {Colors.BOLD}{pass_rate:.1f}%{Colors.END}")
        
        if pass_rate >= 90:
            print(f"\n{Colors.GREEN}{Colors.BOLD}🎉 EXCELLENT! Your webserv is ready for 42 evaluation!{Colors.END}")
        elif pass_rate >= 80:
            print(f"\n{Colors.YELLOW}{Colors.BOLD}⚠️  GOOD, but some issues need attention before evaluation.{Colors.END}")
        else:
            print(f"\n{Colors.RED}{Colors.BOLD}❌ CRITICAL ISSUES found. Fix before evaluation!{Colors.END}")
            
        print(f"\n{Colors.BLUE}Remember: During 42 evaluation, evaluators will test similar edge cases.{Colors.END}")
        print(f"{Colors.BLUE}Your server must NEVER crash and handle all edge cases gracefully.{Colors.END}")

def main():
    """Main function"""
    config_file = sys.argv[1] if len(sys.argv) > 1 else "location_test.conf"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 8080
    
    print(f"{Colors.BLUE}Using config: {config_file}, port: {port}{Colors.END}")
    
    # Check if webserv executable exists
    if not os.path.exists("./webserv"):
        print(f"{Colors.RED}Error: ./webserv executable not found. Please run 'make' first.{Colors.END}")
        sys.exit(1)
        
    # Check if config file exists
    if not os.path.exists(config_file):
        print(f"{Colors.RED}Error: Config file '{config_file}' not found.{Colors.END}")
        sys.exit(1)
        
    tester = WebservTester(config_file, port)
    tester.run_all_tests()

if __name__ == "__main__":
    main()