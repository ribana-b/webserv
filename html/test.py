#!/usr/bin/env python3

import os
import sys

print("Content-Type: text/html")
print("")
print("<h1>Python CGI Test</h1>")
print(f"<p>REQUEST_METHOD: {os.environ.get('REQUEST_METHOD', 'Not set')}</p>")
print(f"<p>QUERY_STRING: {os.environ.get('QUERY_STRING', 'Not set')}</p>")
print(f"<p>Python version: {sys.version}</p>")
