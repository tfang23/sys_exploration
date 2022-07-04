/**
 * File: scheduler.cc
 * ------------------
 * Presents the implementation of the HTTPProxyScheduler class.
 */

#include "scheduler.h"
#include <utility>
using namespace std;

const int nt = 64;

HTTPProxyScheduler::HTTPProxyScheduler(): pool(nt) {}

void HTTPProxyScheduler::scheduleRequest(int clientfd, const string& clientIPAddr) {
    pool.schedule([this, clientfd, clientIPAddr]() {
                      requestHandler.serviceRequest(make_pair(clientfd, clientIPAddr));
                  });
}

void HTTPProxyScheduler::setProxy(const std::string& server, unsigned short port) {
}
