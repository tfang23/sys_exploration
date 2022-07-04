/**
 * File: payload.cc
 * ----------------
 * Presents the implementation of the HTTPPayload 
 * class, as exported by payload.h
 */

#include "payload.h"

#include <string>
#include <iostream>
#include <vector>
#include <iterator>
#include "string-utils.h"

using namespace std;

/** Public methods and functions **/

void HTTPPayload::ingestPayload(const HTTPHeader& header, istream& instream) {
  if (isChunkedPayload(header)) {
    ingestChunkedPayload(instream); 
  } else {
    size_t contentLength = header.getValueAsNumber("Content-Length");
    ingestCompletePayload(instream, contentLength);
  }
}

void HTTPPayload::setPayload(HTTPHeader& header, const string& payload) {
  this->payload.clear();
  appendData(payload);
  header.addHeader("Content-Length", int(payload.size()));
}

ostream& operator<<(ostream& os, const HTTPPayload& hp) {
  copy(hp.payload.begin(), hp.payload.end(), ostream_iterator<char>(os));
  return os;
}

/** Private methods **/

bool HTTPPayload::isChunkedPayload(const HTTPHeader& header) const {
  return header.getValueAsString("Transfer-Encoding") == "chunked";
}

const size_t kHexBase = 16;
void HTTPPayload::ingestChunkedPayload(istream& instream) {
  while (true) {
    string chunkSizeStr;
    getline(instream, chunkSizeStr);
    chunkSizeStr = trim(chunkSizeStr);
    appendData(chunkSizeStr + "\r\n");
    chunkSizeStr.insert(0, "0x");
    unsigned long chunkSize = strtoul(chunkSizeStr.c_str(), NULL, kHexBase);
    vector<char> content(chunkSize + 2); // read \r\n, even when chunk size is 0
    instream.read(&*content.begin(), chunkSize + 2);
    if (chunkSize == 0) break;
    appendData(content);
  }

  appendData("\r\n");
}

void HTTPPayload::ingestCompletePayload(istream& instream, size_t contentLength) {
  vector<char> content(contentLength);
  instream.read(&*content.begin(), contentLength);
  appendData(content);
}

void HTTPPayload::appendData(const string& data) {
  copy(data.begin(), data.end(), back_inserter(this->payload));
}

void HTTPPayload::appendData(const vector<char>& data) {
  copy(data.begin(), data.end(), back_inserter(this->payload));
}
