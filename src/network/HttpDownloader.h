#pragma once
#include <HalStorage.h>
#include <Stream.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

/**
 * HTTP client utility for fetching content and downloading files.
 * Wraps NetworkClientSecure and HTTPClient for HTTPS requests.
 */
class HttpDownloader {
 public:
  using ProgressCallback = std::function<void(size_t downloaded, size_t total)>;
  using HeaderList = std::vector<std::pair<std::string, std::string>>;

  enum DownloadError {
    OK = 0,
    HTTP_ERROR,
    FILE_ERROR,
    ABORTED,
  };

  struct Request {
    std::string method = "GET";   // "GET" or "POST"
    std::string body;             // POST body (ignored when empty)
    std::string contentType;      // Content-Type header for the body
    std::string username;         // Basic auth username
    std::string password;         // Basic auth password
    HeaderList headers;           // Extra headers (e.g. Authorization: Bearer ...)
    int* outHttpCode = nullptr;   // Receives final HTTP status code (optional)
  };

  /**
   * Fetch text content from a URL with optional credentials.
   */
  static bool fetchUrl(const std::string& url, std::string& outContent, const std::string& username = "",
                       const std::string& password = "");

  static bool fetchUrl(const std::string& url, Stream& stream, const std::string& username = "",
                       const std::string& password = "");

  /**
   * Generic request with custom headers, method and optional body.
   * Returns true when the HTTP status code is 200 or 204. The status is also
   * written to request.outHttpCode when set, so callers can react to 401/etc.
   */
  static bool request(const std::string& url, const Request& request, std::string& outContent);

  /**
   * Download a file to the SD card with optional credentials.
   */
  static DownloadError downloadToFile(const std::string& url, const std::string& destPath,
                                      ProgressCallback progress = nullptr, const std::string& username = "",
                                      const std::string& password = "");

  /**
   * Download a file to the SD card supplying custom headers (e.g. Bearer token).
   */
  static DownloadError downloadToFile(const std::string& url, const std::string& destPath, const HeaderList& headers,
                                      ProgressCallback progress);
};
