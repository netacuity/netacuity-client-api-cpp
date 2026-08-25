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
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <libxml/xmlreader.h>
#include "XMLResponse.h"

const int apiServerUdpPort = 5400;       // Query port on the NetAcuity server

// TEST-ONLY accessor; see declaration in XMLResponse.h.
int xmlResponseDefaultApiServerPort()
{
    return apiServerUdpPort;
}

// Returns whether two IP address strings denote the same address, comparing
// parsed addresses rather than raw text so a differently-formatted-but-equal
// IPv6 literal (e.g. compressed vs. expanded) still matches. A malformed
// value is never treated as equal to anything.
static bool ipStringsEqual(const std::string &a, const std::string &b)
{
    struct in_addr  a4, b4;
    if ((inet_pton(AF_INET, a.c_str(), &a4) == 1) && (inet_pton(AF_INET, b.c_str(), &b4) == 1))
    {
        return memcmp(&a4, &b4, sizeof(a4)) == 0;
    }

    struct in6_addr a6, b6;
    if ((inet_pton(AF_INET6, a.c_str(), &a6) == 1) && (inet_pton(AF_INET6, b.c_str(), &b6) == 1))
    {
        return memcmp(&a6, &b6, sizeof(a6)) == 0;
    }

    return false;
}

// A missing transactionID is auto-generated with std::random_device (a CSPRNG)
// rather than a predictable value: a guessable transaction ID would let an
// attacker forge a UDP response that passes the transaction-id echo check in query().
static std::string generateTransactionId()
{
    static const char characters[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static const int  numCharacters = sizeof(characters) - 1;
    static const int  length = 10;

    std::random_device rd;
    std::uniform_int_distribution<int> dist(0, numCharacters - 1);

    std::string id;
    id.reserve(length);
    for (int i = 0; i < length; ++i)
    {
        id += characters[dist(rd)];
    }
    return id;
}

/* Constructors, initialize class variables */
XMLResponse::XMLResponse(int apiId, int timeout):
        responseSize(0), apiID(apiId), timeOut(timeout), serverAddr(""), addressFamily(AF_INET), sockfd(-1)
{

}
XMLResponse::XMLResponse(const std::string &dottedServerIP) :
        responseSize(0), apiID(0), timeOut(2000000), sockfd(-1)
{
    setServerAddr(dottedServerIP);
}
/*
 * Constructor to allow the passing of the NA server ip,
 * and ip to query the server with
 */
XMLResponse::XMLResponse(const std::string &dottedServerIP,
                         const std::string &dottedQueryIP,
                         const std::string &featureCode,
                         const std::string &transactionID,
                         int               apiId,
                         int               timeout) :
        responseSize(0), apiID(apiId), timeOut(timeout), sockfd(-1)
{
    setServerAddr(dottedServerIP);
    query(dottedQueryIP, featureCode, transactionID);
}

/*
 * TEST-ONLY constructor: like the server-only constructor, but overrides
 * the destination UDP port (see destPort). Used by unit tests to point
 * at a MockNaServer bound to an OS-assigned ephemeral port instead of
 * the default NetAcuity protocol port (5400).
 */
XMLResponse::XMLResponse(const std::string &dottedServerAddr, int destPort) :
        responseSize(0), apiID(0), timeOut(2000000), sockfd(-1)
{
    setServerAddr(dottedServerAddr);
    this->destPort = destPort;
}

XMLResponse::~XMLResponse()
{
    if (sockfd != -1)
    {
        DE_CLOSESOCKET(sockfd);
        sockfd = -1;
    }
}

/***************************************************************
 * XMLResponse::parseResponse()
 *             This function parses the fields from the raw
 *             response.
 *
 *      parameters:
 *              map<string> *results : map in which fields are
 *                                       parsed
 *
 *      returns:
 *            nothing, but loads results map
 *
 **************************************************************/
void XMLResponse::parseResponse(StringMap *results)
{
    fieldOrder.clear();
    std::string response_str = response.str();
    xmlDocPtr doc = xmlReadMemory(
                        response_str.c_str(),
                        response_str.length(),
                        "na_response.xml",
                        nullptr,
                        XML_PARSE_NOENT);
    if (doc == nullptr) {
        std::cerr << "Error: could not parse XML response" << std::endl;
        return;
    }
    xmlNodePtr rootNode = doc->children;
    if (rootNode == nullptr) {
        std::cerr << "Error: could not find root element in XML response" << std::endl;
        return;
    }
    xmlAttr* attribute = rootNode->properties;
    while(attribute != nullptr) {
        const xmlChar* xml_name = attribute->name;
        xmlChar* xml_value = xmlNodeListGetString(rootNode->doc, attribute->children, 1);
        std::string name = std::string((char*)xml_name);
        std::string value = std::string((char*)xml_value);
        (*results)[name] = value;
        fieldOrder.push_back(name);
        attribute = attribute->next;
        xmlFree(xml_value);
    }
    xmlFreeDoc(doc);
}

/**********************************************************************
 * XMLResponse::query(string queryIP, featureCode, transactionID)
 *      Querys the NetAcuity Server for the feature codes
 *      passed in.  This is the base XML query function that sends and
 *      and receives from the NetAcuity Server specified.
 *
 *      parameters:
 *                string queryIP   : dotted notation of ip
 *                                    to query.
 *                string featureCode : Database Feature Codes of the
 *                                   databases to be queried
 *                string transactionID : optional; omitted/empty auto-generates one
 *********************************************************************/
bool XMLResponse::query(const std::string &queryIP, const std::string &featureCode, const std::string &transactionID)
{
    // Clear previous response/error so sequential queries don't accumulate state.
    response.str("");
    response.clear();
    responseSize = 0;
    errorMsg = "";

    if ((apiID < 0) || (apiID > 127))
    {
        errorMsg = "Invalid API ID";
        return false;
    }

    // queryIP must be a well-formed IPv4/IPv6 literal: it is spliced unescaped
    // into the request's "ip" attribute below, so a malformed value could
    // otherwise break out of the XML attribute.
    struct in_addr  queryIpv4Check;
    struct in6_addr queryIpv6Check;
    if ((inet_pton(AF_INET, queryIP.c_str(), &queryIpv4Check) != 1) &&
        (inet_pton(AF_INET6, queryIP.c_str(), &queryIpv6Check) != 1))
    {
        errorMsg = "Invalid query IP address";
        return false;
    }

    // A transactionID containing an XML-attribute-breaking character could inject
    // arbitrary XML into the request.
    if (transactionID.find_first_of("\"<>&") != std::string::npos)
    {
        errorMsg = "Invalid transaction ID";
        return false;
    }

    // An omitted (empty) transactionID is auto-generated so the caller
    // doesn't have to invent one just to get a response echo to match against.
    std::string effectiveTransactionID = transactionID.empty() ? generateTransactionId() : transactionID;

    struct sockaddr_in destAddr4;            // struct for NA server
    struct sockaddr_in6 destAddr;            // struct for NA server

    // Setup socket to NA Server
    if (addressFamily == AF_INET)
    {
        sockfd = setupSocket(&destAddr4);
    }
    else
    {
        sockfd = setupSocket6(&destAddr);
    }

    if (sockfd == -1)
    {
        errorMsg = "error setting up socket";
        return false;
    }
    struct timeval tv;                     // timeout struct for select()

    // make timeout structure
    tv.tv_sec = 0;
    tv.tv_usec = timeOut;

    // Deal with Solaris issue with US being > 1000000 (1 sec)
    while (tv.tv_usec >= 1000000)
    {
        tv.tv_usec -= 1000000;
        tv.tv_sec++;
    }

    std::ostringstream requestStream;          // request stream to NA server

    // Build the request
    requestStream << "<request trans-id=\"" << effectiveTransactionID << "\" "
                  << "ip=\"" << queryIP << "\" "
                  << "api-id=\"" << apiID << "\" >";

    std::string::size_type pos;
    std::string::size_type last_pos = 0;
    std::string FC = featureCode;
    if ((pos = featureCode.find(",", last_pos)) == std::string::npos)
    {
        requestStream << "<query db=\"" << FC << "\" />";
    }
    else       
    {
        while ((pos = featureCode.find(",", last_pos)) != std::string::npos)
        {
            FC = featureCode.substr(last_pos, (pos-last_pos));
            if (FC != "")
            {
                /* Make sure featureCode is valid */
                if ((atoi(FC.c_str()) >= 100) || (atoi(FC.c_str()) < 3))
                {
                    errorMsg = "request for feature " + FC + " is invalid";
                    return false;
                }

                requestStream << "<query db=\"" << FC << "\" />";
            }
            last_pos = pos+1;
        }
        FC = featureCode.substr(last_pos);
        if (FC != "")
        {
            /* Make sure featureCode is valid */
            if ((atoi(FC.c_str()) >= 100) || (atoi(FC.c_str()) < 3))
            {
                errorMsg = "request for feature " + FC + " is invalid";
                return false;
            }
            
            requestStream << "<query db=\"" << FC << "\" />";
        }
    }

    requestStream << "</request>";

    int destLen = sizeof(struct sockaddr);     // size of socaddr struct
    int ret = 0;
    if (addressFamily == AF_INET)
    {
        // send request string
        destLen = sizeof(destAddr4);
        ret = sendto(sockfd, requestStream.str().c_str(),
                         requestStream.str().size(), 0,
                         (struct sockaddr *) &destAddr4, destLen);
    }
    else
    {
        // send request string
        destLen = sizeof(destAddr);
        ret = sendto(sockfd, requestStream.str().c_str(),
                         requestStream.str().size(), 0,
                         (struct sockaddr *) &destAddr, destLen);
    }

    if (ret < 1)
    {
        DE_CLOSESOCKET(sockfd);
        sockfd = -1;
        errorMsg = "sendto returned bad return code of " + std::to_string(ret);
        return false;
    }

    bool done = false;
    char naResponse[1501];

    int lastPacketReceived = 0;
    int thisPacket = 0;
    int packetsToReceive = 0;
    std::string inResponse;

    // We need to wait on all packets to be received
    while (!done)
    {
        fd_set fds;                       // descriptor to select() on 
        // wait for the response data
        FD_ZERO(&fds);
        FD_SET(sockfd, &fds);

        /* initialize Memory */
        memset(naResponse, 0, sizeof(naResponse));
               
        // wait until the timeout or we have data from the server
        ret=select(sockfd+1, &fds, nullptr, nullptr, &tv);
        
        // For error or timeout
        if(ret < 1)
        {
            DE_CLOSESOCKET(sockfd);
            sockfd = -1;
            errorMsg = "timeout awaiting response";
            return false;
        }
        
        // Receive information from server
        if (addressFamily == AF_INET)
        {
             ret = recvfrom(sockfd, (char*) &naResponse, sizeof(naResponse)-1, 0,
                          (struct sockaddr *)&destAddr4,
#ifndef WIN32
                       (socklen_t *)
#endif
                       &destLen);
        }
        else
        {
            ret = recvfrom(sockfd, (char*) &naResponse, sizeof(naResponse)-1, 0,
                         (struct sockaddr *)&destAddr,
#ifndef WIN32
                      (socklen_t *)
#endif
                      &destLen);
        }
        
        // Check to see if we got actual good data off socket
        if (ret < 4)
        {
            DE_CLOSESOCKET(sockfd);
            sockfd = -1;
            errorMsg = "received less than minimum return bytes";
            return false;
        }
        naResponse[ret] = '\0';
        inResponse = naResponse;

        // Get which packet this is out of how many there are
        thisPacket = atoi(inResponse.substr(0, 2).c_str());
        packetsToReceive = atoi(inResponse.substr(2, 2).c_str());

        // Make sure we didn't receive the packets out of order
        // If so, we can't put them back together!
        // We have to abort...
        if (thisPacket - 1 != lastPacketReceived)
        {
            DE_CLOSESOCKET(sockfd);
            sockfd = -1;
            errorMsg = "packets received out of order";
            return false;

        }

        // Tack on the response (less the packet crap) to the full
        // response
        response << inResponse.substr(4);

        // If we have received all the packets, then end.
        if (thisPacket == packetsToReceive)
        {
            done = true;
        }
        lastPacketReceived = thisPacket;
    }

    DE_CLOSESOCKET(sockfd);
    sockfd = -1;

    // Reflects the size of the raw response now in `response`, matching
    // getResponse() -- set regardless of whether the trans-id/ip check
    // below ultimately accepts or rejects it.
    responseSize = static_cast<int>(response.str().size());

    // Verify the response actually answers this request before accepting it:
    // a UDP response with a different trans-id/ip could be a stale or
    // spoofed reply to a different query.
    {
        std::string fullResponse = response.str();
        xmlDocPtr doc = xmlReadMemory(
                            fullResponse.c_str(),
                            fullResponse.length(),
                            "na_response.xml",
                            nullptr,
                            XML_PARSE_NOENT);
        if (doc != nullptr)
        {
            xmlNodePtr rootNode = doc->children;
            if (rootNode != nullptr)
            {
                std::string responseTransId;
                std::string responseIp;
                bool haveTransId = false;
                bool haveIp = false;
                xmlAttr *attribute = rootNode->properties;
                while (attribute != nullptr)
                {
                    std::string name = std::string((char *)attribute->name);
                    xmlChar *xmlValue = xmlNodeListGetString(rootNode->doc, attribute->children, 1);
                    std::string value = xmlValue ? std::string((char *)xmlValue) : std::string();
                    if (name == "trans-id")
                    {
                        responseTransId = value;
                        haveTransId = true;
                    }
                    else if (name == "ip")
                    {
                        responseIp = value;
                        haveIp = true;
                    }
                    if (xmlValue != nullptr)
                    {
                        xmlFree(xmlValue);
                    }
                    attribute = attribute->next;
                }
                if ((haveTransId && responseTransId != effectiveTransactionID) ||
                    (haveIp && !ipStringsEqual(responseIp, queryIP)))
                {
                    xmlFreeDoc(doc);
                    // Preserve the genuinely-received response (rather than
                    // overwriting it with a fabricated one) -- getResponse()
                    // should reflect what actually came off the wire.
                    errorMsg = "response trans-id/ip mismatch";
                    return false;
                }
            }
            xmlFreeDoc(doc);
        }
    }

    return true;
}

/***********************************************************
 * XMLResponse::setupSocket
 *          Sets up the socket for communication with the
 *          NetAcuity server.
 *
 *     parameters: pointer to sockaddr_in struct for the
 *                 destination.
 *
 *     returns:   0: Error occured while setting up socket
 *                1: Successfull setup of socket to NA server
 *
 **********************************************************/
DE_SOCKET XMLResponse::setupSocket(struct sockaddr_in *destAddr4)
{
    /* create and check socket, uses UDP */
    /* TCP is not supported any more */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);  
    if (sockfd == -1)
    {
        return -1;
    }

    /* destPort is 0 unless test code has used the test-only (server, port)
     * constructor; production paths always fall back to the default
     * NetAcuity protocol port. */
    destAddr4->sin_port = htons(destPort != 0 ? destPort : apiServerUdpPort);

    /* make it non-blocking, so we can timeout without using signals */
    int ret = setNonBlockFlag(sockfd, 1);
    if (ret == -1)
    {
        DE_CLOSESOCKET(sockfd);
        sockfd = -1;
        return -1;
    }
    /* initialize IP connect structure */
    struct in_addr apiServer;        // struct for server address

    /* setup address of NA server */
    apiServer.s_addr = inet_addr(serverAddr.c_str());

    destAddr4->sin_addr =     apiServer;

    destAddr4->sin_family = AF_INET;        /* host byte order */
    /* PORT number was setup above */
    destAddr4->sin_addr = apiServer;
    memset(&(destAddr4->sin_zero), 1, sizeof(destAddr4->sin_zero));       /* zero the rest of the struct */

    // Restricts the OS to only deliver datagrams from destAddr4 on this socket,
    // rejecting spoofed/stray packets from any other source before this code sees them.
    if (connect(sockfd, (struct sockaddr *) destAddr4, sizeof(*destAddr4)) == -1)
    {
        DE_CLOSESOCKET(sockfd);
        sockfd = -1;
        return -1;
    }

    return sockfd;
}

/***********************************************************
 * XMLResponse::setupSocket6
 *          Sets up the socket for communication with the
 *          NetAcuity server.
 *
 *     parameters: pointer to sockaddr_in6 struct for the
 *                 destination.
 *
 *     returns:   0: Error occured while setting up socket
 *                1: Successfull setup of socket to NA server
 *
 **********************************************************/
DE_SOCKET XMLResponse::setupSocket6(struct sockaddr_in6 *destAddr)
{
    /* setup address of NA server */
    /* create and check socket, uses UDP */
    /* TCP is not supported any more */
    sockfd = socket(PF_INET6, SOCK_DGRAM, 0);
    if (sockfd == -1)
    {
        return -1;
    }

    /* do not use any scope_id */
    destAddr->sin6_scope_id = 0;

    /* set flowinfo to 0 */
    destAddr->sin6_flowinfo = 0;

    /* the port we are going to send to, in network byte order.
     * destPort is 0 unless test code has used the test-only (server, port)
     * constructor; production paths always fall back to the default
     * NetAcuity protocol port. */
    destAddr->sin6_port = htons(destPort != 0 ? destPort : apiServerUdpPort);

    /* make it non-blocking, so we can timeout without using signals */
    int ret = setNonBlockFlag(sockfd, 1);
    if (ret == -1)
    {
          DE_CLOSESOCKET(sockfd);
          sockfd = -1;
          return -1;
    }
    /* initialize IP connect structure */
    /* the server IP address, in network byte order */
    inet_pton(AF_INET6, serverAddr.c_str(), &destAddr->sin6_addr);

    destAddr->sin6_family = AF_INET6;        /* host byte order */

    // Restricts the OS to only deliver datagrams from destAddr on this socket,
    // rejecting spoofed/stray packets from any other source before this code sees them.
    if (connect(sockfd, (struct sockaddr *) destAddr, sizeof(*destAddr)) == -1)
    {
        DE_CLOSESOCKET(sockfd);
        sockfd = -1;
        return -1;
    }

    return sockfd;
}

#ifndef WIN32
//***************************************************
// These is the UNIX-flavor version of setNonBlockFlag()
//***************************************************
/**************************************************************
 * Function:    setNonBlockFlag
 * Returns:     result of calling fcntl
 * Parameters:  file descripter, value (0-off/1-on)
 * Description: Changes the state of the fd's non_block flag
 **************************************************************/
int XMLResponse::setNonBlockFlag (int desc, int value)
{
    int oldflags = fcntl (desc, F_GETFL, 0);
    /* If reading the flags failed, return error indication now. */
    if (oldflags == -1)
        return -1;
    /* Set just the flag we want to set. */
    if (value != 0)
        oldflags |= O_NONBLOCK;
    else
        oldflags &= ~O_NONBLOCK;
    /* Store modified flag word in the descriptor. */
    return fcntl (desc, F_SETFL, oldflags);
}
//***************************************************
// END of Unix-flavor version of setNonBlockFlag()
//***************************************************
#endif

#ifdef WIN32
//***************************************************
// These is the Windows version of setNonBlockFlag()
//***************************************************
/**************************************************************
 * Function:    setNonBlockFlag
 * Returns:     result of calling fcntl
 * Parameters:  file descripter, value (0-off/1-on)
 * Description: Changes the state of the fd's non_block flag
 **************************************************************/
int XMLResponse::setNonBlockFlag (DE_SOCKET desc, int value)
{
    ULONG icmd;
    icmd = value;
    if (ioctlsocket(desc, FIONBIO, &icmd))
    {
        return -1;
    }
    return 1;
}
//***************************************************
// END of Windows version of setNonBlockFlag()
//***************************************************
#endif
