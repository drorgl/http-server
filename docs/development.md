# HTTP Server Development Guide

This document provides comprehensive information for developers working on the HTTP server project, including setup, testing, build configurations, and development tools.

## Table of Contents

1. [Development Workflow](#development-workflow)
2. [PlatformIO Build Environments](#platformio-build-environments)
3. [Testing Procedures](#testing-procedures)
4. [Code Coverage & Analysis](#code-coverage--analysis)
5. [Memory Testing & Sanitization](#memory-testing--sanitization)
6. [Development Tools & Scripts](#development-tools--scripts)
7. [References](#references)

## Development Workflow

### Prerequisites

Install the required development tools:

```bash
# Install PlatformIO
pip install platformio

# Install coverage analysis tools
pip install gcovr

# Install static analysis tools
pip install flawfinder

# Install Valgrind (Linux/macOS)
sudo apt-get install valgrind  # Ubuntu/Debian
brew install valgrind          # macOS
```

### Local Development Testing

The project supports multiple testing environments for different platforms:

#### Windows Native Testing
```bash
pio test -e native_win -f test_esp_http_server -vvv
```

#### Linux Native Testing
```bash
pio test -e native -f test_esp_http_server -vvv
```

#### Full Testing Suite
```bash
pio test -e native
```

### Test Categories

The test suite is organized into logical categories for better maintainability:

- **Server Lifecycle Tests** - Server initialization, configuration, and shutdown
- **URI Handler Management Tests** - Registering, unregistering, and managing URI handlers
- **Request Processing Tests** - Parsing HTTP requests, URL queries, headers, and cookies
- **Response Handling Tests** - Sending HTTP responses, including chunked and custom responses
- **WebSocket Tests** - WebSocket upgrade handshake and data frame exchange
- **Client Management Tests** - Client connection management, limits, and concurrency
- **Error Handling Tests** - HTTP error scenarios and custom error handling
- **Utility Tests** - Utility functions and context management
- **Async Tests** - Asynchronous request and WebSocket handling
- **Memory Tests** - Memory leak detection and sanitization

For detailed test documentation, see [`test/test_esp_http_server/test.md`](../test/test_esp_http_server/test.md).

## PlatformIO Build Environments

The project supports multiple build environments for different platforms and testing scenarios:

### Native Environments

#### `native` (Linux Native)
- **Purpose**: Linux native testing with coverage analysis
- **Features**: Coverage, Debug
- **Usage**: `pio test -e native`

#### `native_win` (Windows Native)
- **Purpose**: Windows native testing with coverage and Windows-specific features
- **Features**: Windows socket support, Coverage, Stack protection
- **Usage**: `pio test -e native_win`

#### `native_sanitizers`
- **Purpose**: Memory and undefined behavior detection
- **Features**: AddressSanitizer, UndefinedBehaviorSanitizer, LeakSanitizer, Frame pointer preservation
- **Usage**: `pio test -e native_sanitizers`

#### `native_valgrind`
- **Purpose**: Comprehensive memory debugging with Valgrind
- **Features**: Valgrind, Frame pointer preservation
- **Usage**: `pio test -e native_valgrind`

### Embedded Environment

#### `esp32s3`
- **Purpose**: ESP32-S3 embedded target development
- **Features**: ESP-IDF, ESP32-S3
- **Usage**: `pio test -e esp32s3`

### Legacy Environment

#### `windows_x86`
- **Purpose**: Windows x86 platform development
- **Features**: Windows, Stack protection, Windows socket support
- **Usage**: `pio test -e windows_x86`

## Testing Procedures

### Running Tests

#### Basic Test Execution
```bash
# Run all tests for a specific environment
pio test -e native

# Run specific test suite
pio test -e native -f test_esp_http_server

# Run with verbose output
pio test -e native -vvv

# Run specific test file (if supported)
pio test -e native --filter "*test_websocket*"
```

#### CI/CD Integration

The project includes a GitHub Actions workflow (`.github/workflows/platformio-test.yml`) that:

1. Builds for ESP32-S3 target
2. Runs Linux native tests
3. Generates coverage reports
4. Performs memory sanitization checks
5. Publishes results to GitHub

### Test Configuration

Each test environment includes specific configurations:

- **Coverage**: All native environments include coverage instrumentation
- **Sanitization**: Specialized environments for memory and behavior checking
- **Platform Support**: Cross-platform compatibility testing
- **Error Handling**: Comprehensive error scenario testing

## Code Coverage & Analysis

### Coverage Analysis with gcovr

Generate comprehensive coverage reports:

```bash
# Basic coverage report
gcovr -v --add-tracefile ".pio/tests/*.json" \
  --root . \
  --exclude test/.* \
  --exclude .pio/.* \
  --exclude lib/http-parser/.* \
  --exclude lib/logger/.* \
  --exclude lib/generic_event_groups/.* \
  --exclude lib/hashing/.* \
  --exclude lib/base64/.* \
  --exclude lib/http-parser/bench.c \
  --exclude lib/http-parser/contrib/.* \
  --exclude lib/http-parser/test.c \
  --html-details .reports/details.html \
  --xml-pretty .reports/coverage.xml \
  --markdown .reports/coverage.md
```

### Static Analysis Tools

#### Flawfinder

Flawfinder is a static analysis tool that identifies potential security vulnerabilities in source code. It scans for common security issues and generates a prioritized list of findings. However, each finding requires manual review within its specific context, as the majority of reported issues are typically false positives.

```bash
# Install and run flawfinder
pip install flawfinder
flawfinder lib/http-server/src/
```

#### Cppcheck
```bash
pio check
```

### Coverage Report Interpretation

- **HTML Reports**: Detailed line-by-line coverage in `.reports/details.html`
- **XML Reports**: Machine-readable coverage data in `.reports/coverage.xml`
- **Markdown Reports**: Summary coverage information in `.reports/coverage.md`

## Memory Testing & Sanitization

### AddressSanitizer (ASan)

Detects memory errors including:
- Buffer overflows and underflows
- Use-after-free errors
- Memory leaks

```bash
# Run with AddressSanitizer
pio test -e native_sanitizers
```

### UndefinedBehaviorSanitizer (UBSan)

Detects undefined behavior including:
- Integer overflow
- Null pointer dereferences
- Invalid type conversions

### Valgrind Memory Analysis

Comprehensive memory debugging:
- Memory leak detection
- Invalid memory access
- Memory pool errors

```bash
# Install Valgrind
sudo apt-get install valgrind

# Run with Valgrind
pio test -e native_valgrind
```

### Memory Testing Workflow

1. **Development**: Use native environments for quick feedback
2. **Integration**: Run sanitizers in CI/CD pipeline
3. **Validation**: Use Valgrind for comprehensive analysis
4. **Reporting**: Review generated logs and reports

## Development Tools & Scripts

### Custom Test Runners

The project includes specialized Python scripts for automated testing:

#### `scripts/gcovr_runner.py`
- **Purpose**: Automated coverage collection and reporting
- **Features**: JSON trace file generation, per-test coverage tracking
- **Usage**: Integrated with PlatformIO test execution

#### `scripts/sanitizer_runner.py`
- **Purpose**: Memory and undefined behavior sanitization
- **Features**: ASan/UBSan integration, error reporting
- **Usage**: Integrated with `native_sanitizers` environment

#### `scripts/valgrind_runner.py`
- **Purpose**: Valgrind memory analysis automation
- **Features**: XML and text report generation
- **Usage**: Integrated with `native_valgrind` environment

#### `scripts/dump_environment.py`
- **Purpose**: Build environment analysis and debugging
- **Features**: Environment variable extraction, JSON output
- **Usage**: Automatic integration with build process

### Environment Variables

Key environment variables for development:

- `PIOTEST_RUNNING_NAME`: Current test name (used by runners)
- `ASAN_OPTIONS`: AddressSanitizer configuration
- `BUILD_DIR`: PlatformIO build directory path

## References

### Documentation Links

- **Coding Standards**: [coding-standards.md](./coding-standards.md) - Essential coding standards and best practices
- **Agent Rules**: [AGENTS.md](./AGENTS.md) - Development guidelines and best practices
- **Test Documentation**: [test/test_esp_http_server/test.md](../test/test_esp_http_server/test.md) - Detailed test organization and procedures
- **CI/CD Workflow**: [.github/workflows/platformio-test.yml](../.github/workflows/platformio-test.yml) - GitHub Actions configuration

### External Resources

- [PlatformIO Documentation](https://docs.platformio.org/)
- [Unity Test Framework](https://www.throwtheswitch.org/unity)
- [gcovr Coverage Tool](https://gcovr.com/)
- [AddressSanitizer Documentation](https://github.com/google/sanitizers/wiki/AddressSanitizer)
- [Valgrind User Manual](https://valgrind.org/docs/manual/manual.html)

### Project Structure

```
http-server/
├── docs/                    # Documentation
│   ├── development.md      # This file
│   └── standards.md        # Coding standards
├── lib/                    # External libraries
│   ├── http-server/        # Main HTTP server library
│   └── [other libraries]/
├── scripts/                # Development scripts
│   ├── gcovr_runner.py     # Coverage analysis
│   ├── sanitizer_runner.py # Memory sanitization
│   ├── valgrind_runner.py  # Valgrind integration
│   └── dump_environment.py # Environment analysis
├── src/                    # Source code
├── test/                   # Test suite
│   └── test_esp_http_server/ # HTTP server tests
├── platformio.ini          # Build configuration
└── .github/workflows/      # CI/CD workflows
```

## Getting Help

If you encounter issues or have questions:

1. **Check the Documentation**: Review this guide and linked documentation
2. **Review Test Output**: Use verbose flags for detailed error information
3. **Environment Analysis**: Use `dump_environment.py` for build debugging
4. **Issue Reporting**: Create detailed bug reports with reproduction steps

---

**Note**: This document is maintained by the development team and updated regularly. For the most current information, refer to the latest version in the repository.