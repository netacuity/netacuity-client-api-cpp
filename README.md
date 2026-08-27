# NetAcuity Client API — C++

C++ client library for querying the [NetAcuity](https://www.digitalelement.com/solutions/netacuity/) Server for IP geolocation and intelligence data. Supports the XML UDP query protocol.

## Requirements

| Dependency | Minimum version | Notes |
|---|---|---|
| C++ compiler | C++17 | GCC 7+, Clang 5+, MSVC 2017+ |
| CMake | 3.14 | For CMake builds |
| libxml2 | any recent | Required for `XMLResponse` |
| Google Test | any recent | Required only to build the test suite |

- A running **NetAcuity Server** accessible on UDP port 5400
- An **API ID** (customer-provided integer, range 0–127; default 0)

**Debian/Ubuntu:**
```bash
sudo apt-get install build-essential cmake libxml2-dev libgtest-dev
```

**RHEL/CentOS/Fedora:**
```bash
sudo dnf install gcc-c++ cmake libxml2-devel gtest-devel
```

**Windows:** Install [vcpkg](https://github.com/microsoft/vcpkg); this repo's `vcpkg.json` manifest declares `libxml2` (and `gtest`, gated behind the `tests` feature) so `vcpkg install` isn't run manually — it's resolved automatically by CMake's vcpkg toolchain integration (see Installation / Build below).
Any built binary that uses `XMLResponse` (including `testXMLAPI`) needs libxml2's DLL on `PATH` at runtime, not just at build time.

## Installation / Build

```bash
git clone https://github.com/netacuity/netacuity-client-api-cpp.git
cd netacuity-client-api-cpp
cmake -S . -B build -DBUILD_TESTS=OFF -DBUILD_EXAMPLES=OFF
cmake --build build
```

**Windows (vcpkg manifest mode):** pass the vcpkg toolchain file so `find_package(LibXml2)`/`find_package(GTest)` can resolve the manifest's dependencies:
```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake -DBUILD_TESTS=OFF -DBUILD_EXAMPLES=OFF
cmake --build build
```

**Troubleshooting a vcpkg install failure:** if `vcpkg install`/the CMake configure step fails to fetch a port file (a 404 or connection error against `raw.githubusercontent.com` or similar), it's usually a transient network hiccup, not a broken pin. First make sure your local vcpkg clone itself is up to date — `git -C %VCPKG_ROOT% pull` — then retry the build; most of these resolve on a second attempt.

## Quick Start

### XML UDP Query (recommended)

The XML UDP protocol supports multiple feature codes in a single query.

```cpp
#include "XMLResponse.h"

XMLResponse xr("192.0.2.100");
bool ok = xr.query("198.51.100.1", "3,8", "txn-001");  // feature codes 3=Geo, 8=ISP

if (ok) {
    XMLResponse::StringMap results;
    xr.parseResponse(&results);

    // query() returns true for a well-formed, matching response, even one
    // reporting a DB-level error -- it returns false only for a failed
    // query itself (invalid input, network/timeout error, or a response
    // whose trans-id/ip doesn't match the request). Always check the
    // error attribute before using field values.
    if (!results["error"].empty()) {
        std::cerr << "Server error: " << results["error"] << "\n";
    }

    std::cout << results["geo-country"] << "\n";
    std::cout << results["isp-name"]    << "\n";
}
```

## API Reference

### `XMLResponse` — XML UDP protocol, supports querying multiple databases in one call

`XMLResponse` is not copyable (it owns a raw socket handle that must not be closed twice); pass it by reference or hold it in a container that doesn't require copies. Server and query addresses may be IPv4 or IPv6 — `setServerAddr`/the address-taking constructors detect the family automatically, and differently-formatted-but-equal IPv6 literals (compressed vs. expanded) are treated as matching when verifying the response's echoed address.

```cpp
XMLResponse(int apiId = 0, int timeout = 2000000);
XMLResponse(const std::string &dottedServerAddr);
XMLResponse(const std::string &dottedServerIP, const std::string &dottedQueryIP,
            const std::string &featureCode, const std::string &transactionID = "",
            int apiId = 0, int timeout = 2000000);

bool query(const std::string &queryIp, const std::string &featureCode, const std::string &transactionID = "");
bool setApiId(int apiId);
bool setTimeout(int timeout);
void setServerAddr(std::string serverIP);
std::string getResponse() const;
int  getResponseSize() const;
std::string getErrorMsg() const;
void parseResponse(StringMap *results);   // StringMap = std::map<std::string, std::string>
const std::vector<std::string>& getFieldOrder() const;
```

| Method | Parameters | Description                                                                                                                                                                                                                                                |
|---|---|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `query` | `queryIp`, `featureCode` (comma-separated feature codes), `transactionID` (optional) | Sends the query; returns `true` for a well-formed, matching response (including one reporting a DB-level error — always check the `error` field), `false` if the response's trans-id or ip doesn't match the request or on failure (check `getErrorMsg()`) |
| `setApiId` | `apiId` (int) | Sets the customer-assigned API ID sent with each query; returns `false` if `apiId` is outside 0–127                                                                                                                                                        |
| `setTimeout` | `timeout` (int, microseconds) | Sets the socket receive timeout; returns `false` if `timeout` is negative                                                                                                                                                                                  |
| `setServerAddr` | `serverIP` (dotted IPv4/IPv6) | Sets/changes the NetAcuity Server address                                                                                                                                                                                                                  |
| `getResponse` | — | The raw, unparsed response text, available alongside the parsed fields                                                                                                                                                                                     |
| `getResponseSize` | — | Returns the size of the raw response                                                                                                                                                                                                                       |
| `getErrorMsg` | — | Returns the last error message, or an empty string if none                                                                                                                                                                                                 |
| `parseResponse` | `results` (`StringMap*`) | Parses the raw XML response into a flat field-name → value map                                                                                                                                                                                             |
| `getFieldOrder` | — | Returns the field names from the most recent `parseResponse()` call in wire order; `StringMap` itself iterates alphabetically, not wire order                                                                                                             |

## Feature Codes

For the complete, up-to-date list of feature codes and their response fields, see the [NetAcuity documentation](https://docs.netacuity.com/).

## Examples

Runnable examples are provided in the `examples/` directory. Build them by enabling `BUILD_EXAMPLES` (the Installation / Build command above disables it):

```bash
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build
```

```bash
./build/testXMLAPI <server_ip> <query_ip> <comma_separated_feature_codes>
```

## Running the Tests

```bash
# Build tests
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build

# Run all tests
cd build && ctest --output-on-failure

# Or run directly with gtest filter
./build/run_tests --gtest_filter=XMLResponse*
```

The test suite's mock server binds to an OS-assigned ephemeral port, so it does not require port 5400 to be free.

## Changelog

See [CHANGELOG.md](CHANGELOG.md) for release history.

## Support

Technical Support is only available to those under active contract with Digital Element. To contact Support, use the contact information provided at contract initiation.

- Documentation: [docs.netacuity.com](https://docs.netacuity.com/)
- Issues: [GitHub Issues](https://github.com/netacuity/netacuity-client-api-cpp/issues)

## License

Copyright 2026 Digital Envoy, Inc.

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE) for the full license text.

This repository contains no third-party source code or binaries. Third-party libraries resolved at build time (libxml2, GoogleTest) are supplied by your package manager and licensed under their own terms (MIT and BSD-3-Clause, respectively).
