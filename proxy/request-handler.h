/**
 * File: request-handler.h
 * -----------------------
 * Defines the HTTPRequestHandler class, which fully proxies and
 * services a single client request.  
 */

#ifndef _request_handler_
#define _request_handler_

#include <utility>
#include <string>
#include <map>
#include <mutex>
#include "request.h"
#include "response.h"
#include "strike-set.h"
#include "cache.h"

class HTTPRequestHandler {
 public:
    HTTPRequestHandler();
    void serviceRequest(const std::pair<int, std::string>& connection) noexcept;
    void clearCache();
    void setCacheMaxAge(long maxAge);
    
 private:
    HTTPCache cache;
    StrikeSet strikeSet;
    mutable std::vector<std::mutex> mutexes;
    
    typedef void (HTTPRequestHandler::*handlerMethod)(HTTPRequest& request, class iosockstream& ss);
    std::map<std::string, handlerMethod> handlers;

    //check if there is a proxy loop
    static bool containsLoop(HTTPRequest& request);

    //add headers
    static void addHeaders(HTTPRequest& request);

    //create client socket
    int configClientSocket(const HTTPRequest& request) const;

    //forward request and get response
    void forwardRequest(HTTPRequest& request, HTTPResponse& response) const;

    //handles all but CONNECT request
    void handleRequest(HTTPRequest& request, class iosockstream& ss);

    //handles CONNECT request
    void handleConnectRequest(HTTPRequest& request, class iosockstream& ss);
     
    void manageClientServerBridge(iosockstream& client, iosockstream& server);
    std::string buildTunnelString(iosockstream& from, iosockstream& to) const;
    void handleBadRequestError(class iosockstream& ss, const std::string& message) const;
    void handleUnsupportedMethodError(class iosockstream& ss, const std::string& message) const;
    void handleError(class iosockstream& ss, const std::string& protocol,
                   HTTPStatus responseCode, const std::string& message) const;    
};

#endif
