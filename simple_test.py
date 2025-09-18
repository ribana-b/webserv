#!/usr/bin/env python3
import subprocess
import time
import requests
import os

print("Testing webserv startup...")

# Start server
print("Starting server...")
server = subprocess.Popen(["./webserv", "location_test.conf"])

time.sleep(3)

# Check if still running
if server.poll() is None:
    print("✅ Server is running")
    
    # Test connection
    try:
        response = requests.get("http://localhost:8080/", timeout=5)
        print(f"✅ Server responding: {response.status_code}")
    except Exception as e:
        print(f"❌ Connection failed: {e}")
        
    # Stop server
    server.terminate()
    server.wait()
    print("✅ Server stopped")
else:
    print(f"❌ Server terminated immediately with code: {server.returncode}")