#pragma once

#include "upload_chunk_feature.h"
#include <HTTPClient.h>
#if CSP_UPLOAD_THROUGHPUT_TRIAL
// Only the file PUT stream path selects this derived client. All endpoint,
// authorization, TLS, response framing and readback methods remain inherited.
class UploadHTTPClient : public HTTPClient {
 public:
  using HTTPClient::sendRequest;
  int sendRequest(const char* type, Stream* stream, size_t size = 0);
};
static_assert(sizeof(UploadHTTPClient) == sizeof(HTTPClient),
              "The upload trial must not add per-client storage");
#else
using UploadHTTPClient = HTTPClient;
#endif

