// Stream method derived from the pinned Arduino HTTPClient.cpp (LGPL-2.1+).
// Copyright (c) 2015 Markus Sattler; ESP32 port by Evandro Luis Copercini.
// Full original license/header/source and exact reviewed diff are preserved in
// sdk/pinned_http_upload. SDK packages remain immutable.
#include "ble_feature.h"
#include "upload_chunk_feature.h"
#if CSP_UPLOAD_THROUGHPUT_TRIAL
#include "upload_chunk_trial.h"
#include <esp32-hal-log.h>
static_assert(HTTP_TCP_TX_BUFFER_SIZE == 1460, "Pinned HTTP TX baseline changed");
static_assert(HTTP_TCP_RX_BUFFER_SIZE == 4096, "Pinned HTTP RX baseline changed");
int UploadHTTPClient::sendRequest(const char *type, Stream *stream, size_t size) {

  if (!stream) {
    return returnError(HTTPC_ERROR_NO_STREAM);
  }

  // connect to server
  if (!connect()) {
    return returnError(HTTPC_ERROR_CONNECTION_REFUSED);
  }

  if (size > 0) {
    addHeader("Content-Length", String(size));
  }

  // add cookies to header, if present
  String cookie_string;
  if (generateCookieString(&cookie_string)) {
    addHeader("Cookie", cookie_string);
  }

  // send Header
  if (!sendHeader(type)) {
    return returnError(HTTPC_ERROR_SEND_HEADER_FAILED);
  }

  int buff_size = CSP_UPLOAD_TX_BYTES;

  int len = size;
  int bytesWritten = 0;

  if (len == 0) {
    len = -1;
  }

  // if possible create smaller buffer then HTTP_TCP_TX_BUFFER_SIZE
  if ((len > 0) && (len < buff_size)) {
    buff_size = len;
  }

  // create buffer for read
  uint8_t *buff = (uint8_t *)malloc(buff_size);

  if (buff) {
    // read all data from stream and send it to server
    while (connected() && (stream->available() > -1) && (len > 0 || len == -1)) {

      // get available data size
      int sizeAvailable = stream->available();

      // Cloud Stick: available() may change after the while admission check.
      // Reject terminal source invalidation before a negative int can become
      // an unbounded size_t passed to readBytes(). Zero retains the SDK yield.
      if (sizeAvailable < 0) {
        free(buff);
        return returnError(HTTPC_ERROR_SEND_PAYLOAD_FAILED);
      }

      if (sizeAvailable) {

        int readBytes = sizeAvailable;

        // read only the asked bytes
        if (len > 0 && readBytes > len) {
          readBytes = len;
        }

        // not read more the buffer can handle
        if (readBytes > buff_size) {
          readBytes = buff_size;
        }

        // read data
        int bytesRead = stream->readBytes(buff, readBytes);
        // A revoked/read-failed source must not produce a successful payload.
        if (bytesRead <= 0 || bytesRead > readBytes) {
          free(buff);
          return returnError(HTTPC_ERROR_SEND_PAYLOAD_FAILED);
        }

        // write it to Stream
        int bytesWrite = _client->write((const uint8_t *)buff, bytesRead);
        bytesWritten += bytesWrite;

        // are all Bytes a written to stream ?
        if (bytesWrite != bytesRead) {
          log_d("short write, asked for %d but got %d retry...", bytesRead, bytesWrite);

          // check for write error
          if (_client->getWriteError()) {
            log_d("stream write error %d", _client->getWriteError());

            //reset write error for retry
            _client->clearWriteError();
          }

          // some time for the stream
          delay(1);

          int leftBytes = (readBytes - bytesWrite);

          // retry to send the missed bytes
          bytesWrite = _client->write((const uint8_t *)(buff + bytesWrite), leftBytes);
          bytesWritten += bytesWrite;

          if (bytesWrite != leftBytes) {
            // failed again
            log_d("short write, asked for %d but got %d failed.", leftBytes, bytesWrite);
            free(buff);
            return returnError(HTTPC_ERROR_SEND_PAYLOAD_FAILED);
          }
        }

        // check for write error
        if (_client->getWriteError()) {
          log_d("stream write error %d", _client->getWriteError());
          free(buff);
          return returnError(HTTPC_ERROR_SEND_PAYLOAD_FAILED);
        }

        // count bytes to read left
        if (len > 0) {
          len -= readBytes;
        }

        delay(0);
      } else {
        delay(1);
      }
    }

    free(buff);

    if (size && (int)size != bytesWritten) {
      log_d("Stream payload bytesWritten %d and size %lu mismatch!.", bytesWritten, (unsigned long)size);
      log_d("ERROR SEND PAYLOAD FAILED!");
      return returnError(HTTPC_ERROR_SEND_PAYLOAD_FAILED);
    } else {
      log_d("Stream payload written: %d", bytesWritten);
    }

  } else {
    log_d("too less ram! need %d", buff_size);
    return returnError(HTTPC_ERROR_TOO_LESS_RAM);
  }

  // handle Server Response (Header)
  return returnError(handleHeaderResponse());
}
#endif
