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
/*
 * test_xml_response.cpp
 *   Unit and integration tests for the XMLResponse class.
 *
 *   Unit tests (no network): constructor defaults.
 *   Integration tests: full UDP round-trip for the XML protocol,
 *   parseResponse attribute extraction, against a MockNaServer bound to an
 *   OS-assigned ephemeral port (see mock_na_server.h).
 *
 *   Depends on libxml2.  Link with: $(xml2-config --libs)
 *
 *   Run:  ./run_tests --gtest_filter=XMLResponse\*
 */
#include <gtest/gtest.h>
#include <map>
#include <string>
#include <vector>

#include "mock_na_server.h"
#include "XMLResponse.h"

static const int FAST_TIMEOUT_US = 200000;  // 200 ms

// ============================================================================
// Constructor defaults
// ============================================================================

TEST(XMLResponse_Constructor, DefaultConstructor_NoError) {
    XMLResponse xr;
    EXPECT_EQ("", xr.getErrorMsg());
}

TEST(XMLResponse_Constructor, DefaultConstructor_EmptyResponse) {
    XMLResponse xr;
    EXPECT_EQ("", xr.getResponse());
}

TEST(XMLResponse_Constructor, DefaultConstructor_ResponseSizeZero) {
    XMLResponse xr;
    EXPECT_EQ(0, xr.getResponseSize());
}

TEST(XMLResponse_Constructor, StringConstructor_IPv4) {
    XMLResponse xr("192.0.2.100");
    EXPECT_EQ("", xr.getErrorMsg());
}

TEST(XMLResponse_Constructor, StringConstructor_IPv6) {
    XMLResponse xr("2001:db8::1");
    EXPECT_EQ("", xr.getErrorMsg());
}

// ============================================================================
// Default destination port (network-free)
// ============================================================================
// The integration tests below override the destination port to point at
// MockNaServer's OS-assigned ephemeral port, so they don't exercise
// XMLResponse's production default of 5400 (the real NetAcuity protocol
// port). Pin it explicitly here instead.

TEST(XMLResponse_DefaultPort, MatchesNetAcuityProtocolPort) {
    EXPECT_EQ(5400, xmlResponseDefaultApiServerPort());
}

// ============================================================================
// Integration tests via MockNaServer (OS-assigned ephemeral port)
// ============================================================================

class XMLResponseMockTest : public ::testing::Test {
protected:
    static MockNaServer* server;

    static void SetUpTestSuite() {
        server = new MockNaServer();
        ASSERT_TRUE(server->start()) << "MockNaServer failed to bind an ephemeral port.";
    }

    static void TearDownTestSuite() {
        server->stop();
        delete server;
        server = nullptr;
    }
};

MockNaServer* XMLResponseMockTest::server = nullptr;

// ---- Single-DB query ----

TEST_F(XMLResponseMockTest, SingleDB_QuerySucceeds) {
    // Simulate a server returning a single-packet XML response.
    std::string xml =
        "<response trans-id=\"txn1\" ip=\"192.0.2.1\" error=\"\" "
        "geo-country=\"usa\" geo-region=\"ca\" geo-city=\"san francisco\" />";
    server->setResponse(MockNaServer::makeXMLPacket(xml));

    XMLResponse xr("127.0.0.1", server->port());
    xr.setTimeout(FAST_TIMEOUT_US);
    int ret = xr.query("192.0.2.1", "3", "txn1");

    EXPECT_EQ(1, ret);
    EXPECT_EQ("", xr.getErrorMsg());
}

TEST_F(XMLResponseMockTest, GetResponseSize_MatchesActualResponseLength) {
    // getResponseSize() must reflect the real size of the assembled response,
    // not just stay at its initial value of 0.
    std::string xml =
        "<response trans-id=\"txn-size\" ip=\"192.0.2.1\" error=\"\" "
        "geo-country=\"usa\" />";
    server->setResponse(MockNaServer::makeXMLPacket(xml));

    XMLResponse xr("127.0.0.1", server->port());
    xr.setTimeout(FAST_TIMEOUT_US);
    ASSERT_EQ(1, xr.query("192.0.2.1", "3", "txn-size"));

    EXPECT_EQ(static_cast<int>(xml.size()), xr.getResponseSize());
    EXPECT_EQ(static_cast<int>(xr.getResponse().size()), xr.getResponseSize());
}

TEST_F(XMLResponseMockTest, SingleDB_ParseResponseExtractsAttributes) {
    std::string xml =
        "<response trans-id=\"txn2\" ip=\"192.0.2.1\" error=\"\" "
        "geo-country=\"usa\" geo-region=\"ca\" geo-city=\"san francisco\" "
        "geo-lat=\"37.7749\" geo-lon=\"-122.4194\" />";
    server->setResponse(MockNaServer::makeXMLPacket(xml));

    XMLResponse xr("127.0.0.1", server->port());
    xr.setTimeout(FAST_TIMEOUT_US);
    ASSERT_EQ(1, xr.query("192.0.2.1", "3", "txn2"));

    XMLResponse::StringMap results;
    xr.parseResponse(&results);

    EXPECT_EQ("usa",           results["geo-country"]);
    EXPECT_EQ("ca",            results["geo-region"]);
    EXPECT_EQ("san francisco", results["geo-city"]);
    EXPECT_EQ("37.7749",       results["geo-lat"]);
    EXPECT_EQ("-122.4194",     results["geo-lon"]);
    EXPECT_EQ("192.0.2.1",       results["ip"]);
    EXPECT_EQ("txn2",          results["trans-id"]);
}

// ---- Multi-DB query (comma-separated feature codes) ----

TEST_F(XMLResponseMockTest, MultipleDBs_ParseResponseExtractsAllAttributes) {
    // A real server would merge fields from multiple DBs into one <response> element.
    std::string xml =
        "<response trans-id=\"txn3\" ip=\"198.51.100.2\" error=\"\" "
        "geo-country=\"deu\" geo-region=\"bavaria\" "
        "isp-name=\"deutsche telekom\" "
        "proxy-type=\"none\" />";
    server->setResponse(MockNaServer::makeXMLPacket(xml));

    XMLResponse xr("127.0.0.1", server->port());
    xr.setTimeout(FAST_TIMEOUT_US);
    ASSERT_EQ(1, xr.query("198.51.100.2", "3,8,12", "txn3"));

    XMLResponse::StringMap results;
    xr.parseResponse(&results);

    EXPECT_EQ("deu",              results["geo-country"]);
    EXPECT_EQ("bavaria",          results["geo-region"]);
    EXPECT_EQ("deutsche telekom", results["isp-name"]);
    EXPECT_EQ("none",             results["proxy-type"]);
}

// ---- Error attribute in response ----

TEST_F(XMLResponseMockTest, ServerError_AttributeInMap) {
    std::string xml =
        "<response trans-id=\"txn4\" ip=\"203.0.113.3\" "
        "error=\"DB Not Loaded\" />";
    server->setResponse(MockNaServer::makeXMLPacket(xml));

    XMLResponse xr("127.0.0.1", server->port());
    xr.setTimeout(FAST_TIMEOUT_US);
    // XMLResponse::query() itself does not inspect the error attribute;
    // parseResponse() populates the map and callers check results["error"].
    ASSERT_EQ(1, xr.query("203.0.113.3", "3", "txn4"));

    XMLResponse::StringMap results;
    xr.parseResponse(&results);

    EXPECT_EQ("DB Not Loaded", results["error"]);
}

// ---- Response IP echo check ----

TEST_F(XMLResponseMockTest, MismatchedResponseIp_QueryFails) {
    // The server echoes back a different IP than the one queried. The
    // mismatch is a query() failure, but the genuinely-received response is
    // preserved as-is (not overwritten with a fabricated stub) -- getResponse()
    // still reflects what actually came off the wire.
    std::string xml =
        "<response trans-id=\"txn-mismatch\" ip=\"203.0.113.99\" error=\"\" "
        "geo-country=\"usa\" />";
    server->setResponse(MockNaServer::makeXMLPacket(xml));

    XMLResponse xr("127.0.0.1", server->port());
    xr.setTimeout(FAST_TIMEOUT_US);
    int ret = xr.query("203.0.113.1", "3", "txn-mismatch");

    EXPECT_EQ(0, ret);
    EXPECT_EQ("response trans-id/ip mismatch", xr.getErrorMsg());
    XMLResponse::StringMap results;
    xr.parseResponse(&results);
    EXPECT_EQ("203.0.113.99", results["ip"]);
    EXPECT_EQ("usa",          results["geo-country"]);
}

TEST_F(XMLResponseMockTest, DbLevelError_QuerySucceedsAndErrorMsgStaysEmpty) {
    // A DB-level error is a successful protocol exchange -- query() returns
    // true and getErrorMsg() stays empty; the error is only visible via the
    // parsed "error" field.
    std::string xml =
        "<response trans-id=\"txn-dberr\" ip=\"203.0.113.1\" error=\"DB Not Loaded\" />";
    server->setResponse(MockNaServer::makeXMLPacket(xml));

    XMLResponse xr("127.0.0.1", server->port());
    xr.setTimeout(FAST_TIMEOUT_US);
    int ret = xr.query("203.0.113.1", "3", "txn-dberr");

    EXPECT_EQ(1, ret);
    EXPECT_EQ("", xr.getErrorMsg());
}

TEST_F(XMLResponseMockTest, DifferentlyFormattedButEqualIPv6ResponseIp_QuerySucceeds) {
    // The server may echo back a compressed form of the same IPv6 address
    // that was queried in expanded form -- this must not be treated as a
    // mismatch.
    std::string xml =
        "<response trans-id=\"txn-ipv6\" ip=\"2001:db8::1\" error=\"\" "
        "geo-country=\"usa\" />";
    server->setResponse(MockNaServer::makeXMLPacket(xml));

    XMLResponse xr("127.0.0.1", server->port());
    xr.setTimeout(FAST_TIMEOUT_US);
    int ret = xr.query("2001:0db8:0000:0000:0000:0000:0000:0001", "3", "txn-ipv6");

    EXPECT_EQ(1, ret);
    XMLResponse::StringMap results;
    xr.parseResponse(&results);
    EXPECT_EQ("", results["error"]);
    EXPECT_EQ("usa", results["geo-country"]);
}

// ---- Empty attribute value ----

TEST_F(XMLResponseMockTest, EmptyAttributeValue_StoredAsEmptyString) {
    std::string xml =
        "<response trans-id=\"txn5\" ip=\"198.51.100.4\" error=\"\" "
        "geo-region=\"\" />";
    server->setResponse(MockNaServer::makeXMLPacket(xml));

    XMLResponse xr("127.0.0.1", server->port());
    xr.setTimeout(FAST_TIMEOUT_US);
    ASSERT_EQ(1, xr.query("198.51.100.4", "3", "txn5"));

    XMLResponse::StringMap results;
    xr.parseResponse(&results);

    EXPECT_EQ("", results["geo-region"]);
}

// ---- getFieldOrder() ----

TEST_F(XMLResponseMockTest, GetFieldOrder_MatchesWireOrderNotAlphabeticalOrder) {
    // geo-region appears before geo-country on the wire, which is the
    // opposite of their alphabetical order in StringMap -- getFieldOrder()
    // must reflect the wire order, not the map's.
    std::string xml =
        "<response trans-id=\"txn-order\" ip=\"198.51.100.5\" error=\"\" "
        "geo-region=\"west\" geo-country=\"usa\" />";
    server->setResponse(MockNaServer::makeXMLPacket(xml));

    XMLResponse xr("127.0.0.1", server->port());
    xr.setTimeout(FAST_TIMEOUT_US);
    ASSERT_EQ(1, xr.query("198.51.100.5", "3", "txn-order"));

    XMLResponse::StringMap results;
    xr.parseResponse(&results);
    std::vector<std::string> expectedOrder = {"trans-id", "ip", "error", "geo-region", "geo-country"};
    EXPECT_EQ(expectedOrder, xr.getFieldOrder());
}

// ---- Multiple sequential queries on the same object ----

TEST_F(XMLResponseMockTest, TwoSequentialQueries_EachReflectsItsOwnResponse) {
    // First query.
    std::string xml1 =
        "<response trans-id=\"q1\" ip=\"192.0.2.1\" error=\"\" "
        "geo-country=\"jpn\" />";
    server->setResponse(MockNaServer::makeXMLPacket(xml1));

    XMLResponse xr("127.0.0.1", server->port());
    xr.setTimeout(FAST_TIMEOUT_US);
    ASSERT_EQ(1, xr.query("192.0.2.1", "3", "q1"));
    {
        XMLResponse::StringMap r1;
        xr.parseResponse(&r1);
        EXPECT_EQ("jpn", r1["geo-country"]);
    }

    // Second query (different XML).
    std::string xml2 =
        "<response trans-id=\"q2\" ip=\"198.51.100.2\" error=\"\" "
        "geo-country=\"bra\" />";
    server->setResponse(MockNaServer::makeXMLPacket(xml2));

    ASSERT_EQ(1, xr.query("198.51.100.2", "3", "q2"));
    {
        XMLResponse::StringMap r2;
        xr.parseResponse(&r2);
        EXPECT_EQ("bra", r2["geo-country"]);
    }
}

// ---- Timeout (no mock server responding) ----

TEST(XMLResponse_Timeout, ReturnsZeroWhenServerUnresponsive) {
    XMLResponse xr("192.0.2.1");  // TEST-NET-1, RFC 5737
    xr.setTimeout(FAST_TIMEOUT_US);
    int ret = xr.query("192.0.2.1", "3", "timeout-txn");
    EXPECT_EQ(0, ret);
    EXPECT_FALSE(xr.getErrorMsg().empty());
    // Nothing was ever received from the server, so no response is fabricated.
    EXPECT_EQ("", xr.getResponse());
}

// ---- setServerAddr / setApiId / setTimeout do not crash ----

TEST(XMLResponse_Setters, DoNotCrash) {
    XMLResponse xr;
    EXPECT_NO_THROW(xr.setServerAddr("192.0.2.100"));
    EXPECT_NO_THROW(xr.setApiId(42));
    EXPECT_NO_THROW(xr.setTimeout(500000));
}

// ============================================================================
// setApiId() / setTimeout() validation
// ============================================================================

TEST(XMLResponse_ApiId, SetApiId_NegativeOne_RejectedAndErrorSet) {
    XMLResponse xr;
    EXPECT_FALSE(xr.setApiId(-1));
    EXPECT_EQ("Invalid API ID", xr.getErrorMsg());
}

TEST(XMLResponse_ApiId, SetApiId_Code128_RejectedAndErrorSet) {
    XMLResponse xr;
    EXPECT_FALSE(xr.setApiId(128));
    EXPECT_EQ("Invalid API ID", xr.getErrorMsg());
}

TEST(XMLResponse_ApiId, SetApiId_InRange_Accepted) {
    XMLResponse xr;
    EXPECT_TRUE(xr.setApiId(42));
    EXPECT_EQ("", xr.getErrorMsg());
}

TEST(XMLResponse_Timeout_Setter, NegativeValue_RejectedAndErrorSet) {
    XMLResponse xr;
    EXPECT_FALSE(xr.setTimeout(-1));
    EXPECT_EQ("Invalid timeout", xr.getErrorMsg());
}

TEST(XMLResponse_Timeout_Setter, NonNegativeValue_Accepted) {
    XMLResponse xr;
    EXPECT_TRUE(xr.setTimeout(0));
    EXPECT_EQ("", xr.getErrorMsg());
}

// query()'s own apiID range check is defense-in-depth: setApiId() rejects an
// out-of-range value before it's ever assigned, so this exercises query()'s
// check via the optional constructor parameter, which bypasses setApiId().
TEST(XMLResponse_ApiId, InvalidConstructorApiId_QueryFails) {
    XMLResponse xr(999);
    xr.setServerAddr("127.0.0.1");
    int ret = xr.query("192.0.2.1", "3", "txn");
    EXPECT_EQ(0, ret);
    EXPECT_EQ("Invalid API ID", xr.getErrorMsg());
}