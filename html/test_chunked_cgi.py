#!/usr/bin/env python3
"""
CGI script to test chunked encoding
Reads all data from stdin and reports how many bytes were received
"""
import sys
import os

# Read CONTENT_LENGTH from environment
content_length = int(os.environ.get('CONTENT_LENGTH', 0))

# Read all data from stdin
bytes_read = 0
chunk_size = 8192

while True:
    chunk = sys.stdin.buffer.read(chunk_size)
    if not chunk:
        break
    bytes_read += len(chunk)

# Output HTTP response
print("Content-Type: text/html\r")
print("\r")
print("<html><body>")
print(f"<h1>Chunked CGI Test</h1>")
print(f"<p>CONTENT_LENGTH: {content_length}</p>")
print(f"<p>Bytes read from stdin: {bytes_read}</p>")
if bytes_read == content_length:
    print("<p style='color:green;font-weight:bold;'>SUCCESS: All bytes received!</p>")
else:
    print(f"<p style='color:red;font-weight:bold;'>ERROR: Expected {content_length} but got {bytes_read}</p>")
print("</body></html>")
