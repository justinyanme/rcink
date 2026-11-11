#pragma once
#include "WiFiClientSecure.h"
constexpr int HTTPC_DISABLE_FOLLOW_REDIRECTS = 0;
class HTTPClient {
 public:
  void setConnectTimeout(unsigned) {}
  void setTimeout(unsigned) {}
  void setFollowRedirects(int policy) { http_fixture::redirects_disabled = policy == HTTPC_DISABLE_FOLLOW_REDIRECTS; }
  void useHTTP10(bool) {}
  bool begin(WiFiClientSecure& stream, const char*) { stream_ = &stream; return true; }
  void collectHeaders(const char**, unsigned) {}
  void setUserAgent(const char*) {}
  void addHeader(const char*, const char*) {}
  int GET() { http_fixture::requests++; return http_fixture::status; }
  String header(const char* name) { return strcmp(name, "Content-Type") == 0 ? http_fixture::media : http_fixture::transfer; }
  int getSize() { return http_fixture::declared; }
  WiFiClient* getStreamPtr() { return stream_; }
  bool connected() { return http_fixture::stalled || stream_->available() > 0; }
  void end() {}
 private:
  WiFiClient* stream_ = nullptr;
};
