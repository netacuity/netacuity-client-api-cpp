/*
 * Copyright 2026 Digital Envoy, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef XMLRESPONSE_H
#define XMLRESPONSE_H 

#include <iostream>
#include <string>
#include <sstream>
#include <sys/types.h>
#include <fcntl.h>
#include <map>
#include <vector>
#include <cstdlib>
#include <cstring>

#ifndef WIN32
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#define DE_SOCKET int
#define DE_CLOSESOCKET close
#else
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <WS2tcpip.h>
 #include <windows.h>
 #define DE_SOCKET SOCKET
 #define DE_CLOSESOCKET closesocket
#endif

// TEST-ONLY: exposes the compiled-in default NetAcuity protocol UDP port
// (XMLResponse.cpp's file-scope apiServerUdpPort constant) so a network-free
// test can assert it hasn't drifted from 5400. Production code should use
// the class API instead of calling this directly.
int xmlResponseDefaultApiServerPort();

class XMLResponse
{
  public:
    /* Constructors */
    // apiId/timeout are optional so a caller doesn't have to follow up with
    // setApiId()/setTimeout() just to configure them before query().
    XMLResponse(int apiId = 0, int timeout = 2000000);

    /* Takes in the dotted ip address of the NetAcuity server */
    XMLResponse(const std::string &dottedServerAddr);

    /* Parameters:
     *      string dottedServerIP:  dotted IP address of NetAcuity server
     *      string dottedQueryIP :  dotted IP address to query
     *      string featureCode     :  feature codes of the DBs to query
     *      string transactionID   :  optional; omitted/empty auto-generates one
     *      int apiId, int timeout :  optional; set before the query() below fires
     */
    XMLResponse(const std::string &dottedServerIP, const std::string &dottedQueryIP,
                const std::string &featureCode, const std::string &transactionID="",
                int apiId = 0, int timeout = 2000000);
    /* TEST-ONLY: like the server-only constructor above, but overrides the
     * destination UDP port instead of using the default NetAcuity protocol
     * port (5400). Intended for pointing at a mock server bound to an
     * OS-assigned ephemeral port during unit tests; production code should
     * use one of the other constructors so it keeps targeting port 5400. */
    XMLResponse(const std::string &dottedServerAddr, int destPort);
    ~XMLResponse();

    /* Not copyable: owns a raw socket handle that must not be closed twice. */
    XMLResponse(const XMLResponse &) = delete;
    XMLResponse &operator=(const XMLResponse &) = delete;

    // Query databases. An omitted/empty transactionID is auto-generated.
    bool  query(const std::string &queryIp, const std::string &featureCode, const std::string &transactionID="");

    // Accessor functions
    bool setApiId(int apiId);               // Set the API id; false if out of range
    bool setTimeout(int timeout);           // Set the timeout for the socket; false if negative
    void setServerAddr(std::string serverIP);    // Set address of NA server
    int getResponseSize() const;              // Get response size
    std::string getResponse() const;               // Get raw response

    std::string getErrorMsg() const;
    /*
     * Function to parse the response
     */
     typedef std::map<std::string, std::string> StringMap;
     void parseResponse(StringMap *results);

    // StringMap iterates in alphabetical key order, not wire order. This
    // returns the field names from the most recent parseResponse() call in
    // the order libxml2 handed them back, which does preserve document
    // (wire) order, for callers who need it.
    const std::vector<std::string>& getFieldOrder() const;

  private:
    std::ostringstream response;            // NA raw response for database queried
    int responseSize = 0;              // Size of the raw response
    int apiID = 0;                      // API ID
    int timeOut = 2000000;             // timeout of socket
    std::string serverAddr;      // ip address of server
    std::string                 errorMsg;
    int addressFamily = AF_INET;       // Address family AF_INET or AF_INET6
    DE_SOCKET sockfd = -1;
    int destPort = 0;                  // 0 == use the default NetAcuity protocol port;
                                        // non-zero only ever set by the test-only (server, port) constructor
    std::vector<std::string> fieldOrder;   // field names in wire order, set by parseResponse()
    
    /* Set the socket to NA server as non blocking */
    int setNonBlockFlag(DE_SOCKET desc, int value);
    /* Setup the socket to NA server for sending and receiving */
    DE_SOCKET setupSocket(struct sockaddr_in *destAddr);
    DE_SOCKET setupSocket6(struct sockaddr_in6 *destAddr);
};

/* Sets up the API ID */
inline bool XMLResponse::setApiId(int inApiID)
{
    if ((inApiID < 0) || (inApiID > 127))
    {
        errorMsg = "Invalid API ID";
        return false;
    }
    apiID = inApiID;
    return true;
}

/* Sets the timeout of the socket to the NetAcuity server */
inline bool XMLResponse::setTimeout(int inTimeout)
{
    if (inTimeout < 0)
    {
        errorMsg = "Invalid timeout";
        return false;
    }
    timeOut = inTimeout;
    return true;
}

/* Set the address of the NetAcuity server */
inline void XMLResponse::setServerAddr(std::string dottedServerIP)
{
  if (dottedServerIP.find(":") == std::string::npos)
  {
      // Assume this one is an IPv4 address.
      addressFamily = AF_INET;
  }
  else
  {
     // Assume this one is an IPv6 address.
     addressFamily = AF_INET6;
  }

  serverAddr = dottedServerIP;
}

/*
 * Returns the raw response size
 * (including the /n at the end of the response)
 */
inline int XMLResponse::getResponseSize() const
{
    return responseSize;
}

/* Returns the raw response */
inline std::string XMLResponse::getResponse() const
{
    return response.str();
}

inline std::string XMLResponse::getErrorMsg() const
{
     return errorMsg;
}

inline const std::vector<std::string>& XMLResponse::getFieldOrder() const
{
    return fieldOrder;
}

#endif
