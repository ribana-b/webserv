#!/bin/bash

# CGI script debe generar headers HTTP primero
echo "Content-Type: text/html"
echo ""

# Generar respuesta HTML
echo "<h1>Hello from Native CGI!</h1>"
echo "<p>This is a bash CGI script</p>"
echo "<p><strong>Environment Variables:</strong></p>"
echo "<ul>"
echo "<li><strong>REQUEST_METHOD</strong>: $REQUEST_METHOD</li>"
echo "<li><strong>QUERY_STRING</strong>: $QUERY_STRING</li>"
echo "<li><strong>SCRIPT_NAME</strong>: $SCRIPT_NAME</li>"
echo "<li><strong>SERVER_SOFTWARE</strong>: $SERVER_SOFTWARE</li>"
echo "<li><strong>CONTENT_TYPE</strong>: $CONTENT_TYPE</li>"
echo "<li><strong>CONTENT_LENGTH</strong>: $CONTENT_LENGTH</li>"
echo "</ul>"

# Mostrar fecha/hora actual
echo "<p><strong>Current date/time</strong>: $(date)</p>"

# Si es POST, leer el body del stdin
if [ "$REQUEST_METHOD" = "POST" ]; then
    echo "<p><strong>POST Data received:</strong></p>"
    echo "<pre>"
    cat
    echo "</pre>"
fi