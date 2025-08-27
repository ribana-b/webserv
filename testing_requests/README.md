# HttpRequest Testing Suite

Este directorio contiene pruebas comprehensivas para la clase `HttpRequest` del proyecto webserv.

## Estructura

```
testing_requests/
├── test_httprequest.cpp    # Suite de pruebas principal
├── Makefile               # Makefile para compilar y ejecutar tests
└── README.md              # Este archivo
```

## Cómo usar

### Compilar y ejecutar tests
```bash
cd testing_requests
make test
```

### Solo compilar
```bash
make
```

### Limpiar archivos generados
```bash
make clean
```

### Ver ayuda
```bash
make help
```

## Tests incluidos

### 1. Basic GET Request
- ✅ Parsing de method, path, version
- ✅ Headers básicos (Host, User-Agent, Accept)
- ✅ Requests sin body (Content-Length = 0)

### 2. POST Request with Body
- ✅ Parsing de requests con Content-Length
- ✅ Body data completo
- ✅ Headers Content-Type

### 3. DELETE Request  
- ✅ Método DELETE
- ✅ Headers de Authorization
- ✅ Paths con parámetros (/api/user/123)

### 4. Case-Insensitive Headers
- ✅ Headers en mayúsculas (HOST)
- ✅ Headers en minúsculas (host) 
- ✅ Headers mixtos (User-Agent)
- ✅ Verificación que todos devuelven el mismo valor

### 5. Invalid Requests Validation
- ✅ Métodos HTTP inválidos (INVALID)
- ✅ Requests sin versión HTTP
- ✅ Paths que no empiezan con /
- ✅ Requests incompletos (sin \\r\\n\\r\\n)
- ✅ Body size mismatch (Content-Length vs datos reales)

### 6. Edge Cases
- ✅ Headers con valores vacíos
- ✅ Paths muy largos con parámetros
- ✅ Content-Length: 0 con POST

## Interpretación de resultados

- **[PASS]** - Test exitoso ✅
- **[FAIL]** - Test falló ❌
- **REJECTED ✓** - Request inválido rechazado correctamente
- **ACCEPTED ✗** - Request inválido aceptado erróneamente (ERROR)

## Ejemplo de salida exitosa

```
=====================================================
           HttpRequest Comprehensive Test Suite     
=====================================================

===========================================
  Basic GET Request
===========================================
Method: 'GET'
Path: '/index.html'
Version: 'HTTP/1.1'
Host header: 'localhost:8080'
[PASS] Basic GET parsing

... (más tests)

=====================================================
                    TEST SUMMARY                     
=====================================================
Tests Passed: 6/6
🎉 ALL TESTS PASSED! HttpRequest is ready for production.
=====================================================
```

## Notas de desarrollo

- Tests siguen estándares de 42 (C++98, headers apropiados)
- Compatible con tu arquitectura existente (Logger, colours)
- Fácilmente extensible para agregar más casos de test
- Usa colores para output más legible
- Exit code 0 si todos pasan, 1 si fallan (útil para CI/CD)

## Agregar nuevos tests

Para agregar un nuevo test, crea una función siguiendo el patrón:

```cpp
bool testNewFeature() {
    printTestHeader("New Feature Test");
    
    Logger logger(std::cout, true);
    HttpRequest request(logger);
    
    // Tu test aquí...
    
    bool success = /* condiciones de éxito */;
    printResult(success, "New feature description");
    return success;
}
```

Y agrégala al main() incrementando `totalTests`.