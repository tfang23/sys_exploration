/**
 * File: request-handler.cc
 * ------------------------
 * Provides the implementation for the HTTPRequestHandler class.
 */

#include "request-handler.h"
#include "response.h"
#include <socket++/sockstream.h> // for sockbuf, iosockstream
#include "ostreamlock.h"
#include "client-socket.h"
#include "watchset.h"

using namespace std;

static const int mnum = 997;
static const string kDefaultProtocol = "HTTP/1.0";
static const string comma = ", ";
static const string ff = "x-forwarded-for";

HTTPRequestHandler::HTTPRequestHandler(): mutexes(mnum) {
  handlers["GET"] = &HTTPRequestHandler::handleRequest;
  handlers["POST"] = &HTTPRequestHandler::handleRequest;
  handlers["HEAD"] = &HTTPRequestHandler::handleRequest;
  handlers["CONNECT"] = &HTTPRequestHandler::handleConnectRequest;
  strikeSet.addFrom("blocked-domains.txt");
}

bool HTTPRequestHandler::containsLoop(HTTPRequest& request) {
    if (!request.containsName(ff)) return false;
    
    std::string clientip = request.getip();
    std::string ips = request.getHeader().getValueAsString(ff);

    //check each ip against client ip
    size_t pos = 0;
    while ((pos = ips.find(comma)) != std::string::npos) {
        std::string pip = ips.substr(0, pos);
        if (clientip == pip) return true;
        ips.erase(0, pos + comma.length());
    }
    return false;
}

void HTTPRequestHandler::serviceRequest(const pair<int, string>& connection) noexcept {
    sockbuf sb(connection.first);
    iosockstream ss(&sb);
    
    try {
        HTTPRequest request;
        request.ingestRequestLine(ss);
        request.ingestHeader(ss, connection.second);
        request.ingestPayload(ss);

        //check if the server is blocked
        if (strikeSet.contains(request.getServer())) {
            handleError(ss, kDefaultProtocol, HTTPStatus::Forbidden, "Forbidden Content");
            return;
        }
        
        //check if there is a proxy loop
        if (containsLoop(request)) {
            handleError(ss, kDefaultProtocol, HTTPStatus::BadRequest, "Loop Detected");
            return;
        }
    
        auto found = handlers.find(request.getMethod());
        if (found == handlers.cend())
            throw UnsupportedMethodExeption(request.getMethod());
        (this->*(found->second))(request, ss);
    } catch (const HTTPBadRequestException &bre) {
        handleBadRequestError(ss, bre.what());
    } catch (const UnsupportedMethodExeption& ume) {
        handleUnsupportedMethodError(ss, ume.what());
    } catch (...) {}
}

void HTTPRequestHandler::addHeaders(HTTPRequest& request) {
    request.addHeader("x-forwarded-proto", "http");
    
    //manipulate values of x-forwarded-for
    if (request.containsName(ff)) {
        const std::string& oldips = request.getHeader().getValueAsString(ff);
        const std::string& clientip = request.getip();
        const std::string& newips = oldips + comma + clientip;
        request.addHeader(ff, newips);
    } else {
        request.addHeader(ff, request.getip());
    }
}

int HTTPRequestHandler::configClientSocket(const HTTPRequest& request) const {
    cout << oslock << "Creating client socket" << endl << osunlock;
    int client = createClientSocket(request.getServer(), request.getPort());
    return client;
} 

void HTTPRequestHandler::forwardRequest(HTTPRequest& request, HTTPResponse& response) const {
    //create client socket
    sockbuf sb(configClientSocket(request));
    iosockstream ss(&sb);

    //add request header
    addHeaders(request);
    ss << request << flush;

    //ingest response header
    response.ingestResponseHeader(ss);

    //ingest response payload
    if (request.getMethod() != "HEAD") response.ingestPayload(ss);
}    

void HTTPRequestHandler::handleRequest(HTTPRequest& request, class iosockstream& ss) {
    cout << oslock << "Handling " << request.getMethod() << " request" << endl << osunlock;
    HTTPResponse response;

    //acquire a mutex
    size_t index = cache.hashRequest(request) % mutexes.size();
    std::unique_lock<std::mutex> ul(mutexes[index]);

    //read from cache if possible
    if (cache.containsCacheEntry(request, response)) {
        cout << oslock << "Reading from cache" << endl << osunlock;
        try {
            ss << response << flush;
            return;
        } catch(const HTTPResponseException& rpe) {
            handleError(ss, kDefaultProtocol, HTTPStatus::GeneralProxyFailure, rpe.what());
        }        
    }  
    ul.unlock();

    try {
        //forward request if possible
        forwardRequest(request, response);
    } catch(const HTTPRequestException& rqe) {
        handleError(ss, kDefaultProtocol, HTTPStatus::GeneralProxyFailure, rqe.what());
    }

    //add to cache if possible
    ul.lock();
    if (cache.shouldCache(request, response)) cache.cacheEntry(request, response);
    ul.unlock();

    //send response
    cout << oslock << "Sending response to client" << endl << osunlock;
    try {
        ss << response << flush;
    } catch (const HTTPResponseException& rpe) {
        handleError(ss, kDefaultProtocol, HTTPStatus::GeneralProxyFailure, rpe.what());
    }
}

void HTTPRequestHandler::handleConnectRequest(HTTPRequest& request, class iosockstream& cs) {
    cout << oslock << "Handling CONNECT request" << endl << osunlock;
    try {
        //create client socket
        sockbuf sb(configClientSocket(request));
        iosockstream ss(&sb);
        handleError(cs, kDefaultProtocol, HTTPStatus::OK, "OK");
        manageClientServerBridge(cs, ss);
    } catch (const HTTPProxyException& pe) {
        handleError(cs, kDefaultProtocol, HTTPStatus::GeneralProxyFailure, pe.what());
    }
}

const size_t kTimeout = 5;
const size_t kBridgeBufferSize = 1 << 16;
void HTTPRequestHandler::manageClientServerBridge(iosockstream& client, iosockstream& server) {
  // get embedded descriptors leading to client and origin server
  int clientfd = client.rdbuf()->sd();
  int serverfd = server.rdbuf()->sd();

  // monitor both descriptors for any activity
  ProxyWatchset watchset(kTimeout);
  watchset.add(clientfd);
  watchset.add(serverfd);

  // map each descriptor to its surrounding iosockstream and the one
  // surrounding the descriptor on the other side of the bridge we're building
  map<int, pair<iosockstream *, iosockstream *>> streams;
  streams[clientfd] = make_pair(&client, &server);
  streams[serverfd] = make_pair(&server, &client);
  cout << oslock << buildTunnelString(client, server) << "Establishing HTTPS tunnel" << endl << osunlock;

  while (!streams.empty()) {
    int fd = watchset.wait();
    if (fd == -1) break; // return value of -1 means we timed out
    iosockstream& from = *streams[fd].first;
    iosockstream& to = *streams[fd].second;
    char buffer[kBridgeBufferSize];
    from.read(buffer, 1); // attempt to read one byte to see if we have one
    if (from.eof() || from.fail() || from.gcount() == 0) {
       // in here? that's because the watchset detected EOF instead of an unread byte
       watchset.remove(fd);
       streams.erase(fd);
       continue;
    }
    to.write(buffer, 1);

    while (true) {
        //read bytes
        size_t nBytesRead = from.readsome(buffer, sizeof(buffer));
        if (from.eof() || from.fail()) {
            watchset.remove(fd);
            streams.erase(fd);
            break;
        };
        //quit if no bytes read
        if (nBytesRead == 0) break;
        cout << oslock << nBytesRead << " bytes read" << endl << osunlock;

        //write the bytes read
        to.write(buffer, nBytesRead);
    }
    to.flush();
  }
  cout << oslock << buildTunnelString(client, server) << "Tearing down HTTPS tunnel" << endl << osunlock;
}

string HTTPRequestHandler::buildTunnelString(iosockstream& from, iosockstream& to) const {
  return "[" + to_string(from.rdbuf()->sd()) + " --> " + to_string(to.rdbuf()->sd()) + "]: ";
}

/**
 * Responds to the client with code 400 and the supplied message.
 */
void HTTPRequestHandler::handleBadRequestError(iosockstream& ss, const string& message) const {
  handleError(ss, kDefaultProtocol, HTTPStatus::BadRequest, message);
}

/**
 * Responds to the client with code 405 and the provided message.
 */
void HTTPRequestHandler::handleUnsupportedMethodError(iosockstream& ss, const string& message) const {
  handleError(ss, kDefaultProtocol, HTTPStatus::MethodNotAllowed, message);
}

/**
 * Generic error handler used when our proxy server
 * needs to invent a response because of some error.
 */
void HTTPRequestHandler::handleError(iosockstream& ss, const string& protocol,
                                     HTTPStatus responseCode, const string& message) const {
  HTTPResponse response;
  response.setProtocol(protocol);
  response.setResponseCode(responseCode);
  response.setPayload(message);
  ss << response << flush;
}

// the following two methods needs to be completed 
// once you incorporate your HTTPCache into your HTTPRequestHandler
void HTTPRequestHandler::clearCache() {
    cache.clear();
}
void HTTPRequestHandler::setCacheMaxAge(long maxAge) {
    cache.setMaxAge(maxAge);
}
