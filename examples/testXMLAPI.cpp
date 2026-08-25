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
#include "XMLResponse.h"
#include <iostream>
#include <random>

int main(int argc, char *argv[])
{
    /* Checking on command line arguements */
    if ( argc != 4 )
    {
        std::cout << "Syntax: " << argv[0]
             << " <server_ip> <query_ip> <comma-separated feature_codes>" << std::endl;
        exit(-1);
    }

#ifdef WIN32
    WSADATA wsData;               // Used to init Win Socket.
    WORD wVer = MAKEWORD(2,2);    // Used to init Win Socket.

    /* Start the Win Sock system. */
    int status = WSAStartup(wVer,&wsData);
    if (status != NO_ERROR)
    {
        printf("Error in WSAStartup.\n");
        exit(-1);
    }
#define DE_WSACLEANUP WSACleanup()
#else
#define DE_WSACLEANUP 1
#endif

    int exit_code = 0;

    const char *server_ip = argv[1];
    const char *query_ip = argv[2];
    const char *feature_codes = argv[3];
    int example_api_id = 72;
    int timeoutMicroseconds = 3000000;  // 3 seconds

    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<int> trans_id_dist(0, 999999999);
    std::ostringstream trans_id_stream;
    trans_id_stream << trans_id_dist(gen);
    std::string transaction_id = trans_id_stream.str();
    
    XMLResponse naXML(server_ip);
    naXML.setApiId(example_api_id);
    naXML.setTimeout(timeoutMicroseconds);

    if (!naXML.query(query_ip, feature_codes, transaction_id))
    {
        std::cout << "Error: " << naXML.getErrorMsg() << std::endl;
        DE_WSACLEANUP;
        exit(-1);
    }

    XMLResponse::StringMap myMap;
    naXML.parseResponse(&myMap);
    std::cout << "ip = " << myMap["ip"] << std::endl;
    std::cout << "trans-id = " << myMap["trans-id"] << std::endl;
    XMLResponse::StringMap::iterator pos;
    for (pos = myMap.begin(); pos != myMap.end(); ++pos)
    {
        if (pos->first == "ip" || pos->first == "trans-id") {
            continue;
        }
        std::cout << pos->first << " = " << pos->second << std::endl;
        if (pos->first == "error") {
            exit_code = -1;
        }
    }
    std::cout << "raw-response = " << naXML.getResponse() << std::endl;

    DE_WSACLEANUP;
    exit(exit_code);
}
