#include "HttpDownloader.h"

#include <HTTPClient.h>
#include <Logging.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>
#include <StreamString.h>
#include <base64.h>

#include <cstring>
#include <memory>
#include <utility>

#include "AppVersion.h"
#include "util/UrlUtils.h"

namespace {
class FileWriteStream final : public Stream {
 public:
  FileWriteStream(FsFile& file, size_t total, HttpDownloader::ProgressCallback progress)
      : file_(file), total_(total), progress_(std::move(progress)) {}

  size_t write(uint8_t byte) override { return write(&byte, 1); }

  size_t write(const uint8_t* buffer, size_t size) override {
    // Write-through stream for HTTPClient::writeToStream with progress tracking.
    const size_t written = file_.write(buffer, size);
    if (written != size) {
      writeOk_ = false;
    }
    downloaded_ += written;
    if (progress_ && total_ > 0) {
      progress_(downloaded_, total_);
    }
    return written;
  }

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override { file_.flush(); }

  size_t downloaded() const { return downloaded_; }
  bool ok() const { return writeOk_; }

 private:
  FsFile& file_;
  size_t total_;
  size_t downloaded_ = 0;
  bool writeOk_ = true;
  HttpDownloader::ProgressCallback progress_;
};
}  // namespace

namespace {
std::unique_ptr<NetworkClient> makeClientFor(const std::string& url) {
  std::unique_ptr<NetworkClient> client;
  if (UrlUtils::isHttpsUrl(url)) {
    auto* secureClient = new NetworkClientSecure();
    secureClient->setInsecure();
    client.reset(secureClient);
  } else {
    client.reset(new NetworkClient());
  }
  return client;
}

void applyAuthAndHeaders(HTTPClient& http, const std::string& username, const std::string& password,
                        const HttpDownloader::HeaderList& headers) {
  http.addHeader("User-Agent", "CrossInk-ESP32-" CROSSINK_VERSION);
  if (!username.empty() && !password.empty()) {
    std::string credentials = username + ":" + password;
    String encoded = base64::encode(credentials.c_str());
    http.addHeader("Authorization", "Basic " + encoded);
  }
  for (const auto& [name, value] : headers) {
    http.addHeader(String(name.c_str()), String(value.c_str()));
  }
}
}  // namespace

bool HttpDownloader::fetchUrl(const std::string& url, Stream& outContent, const std::string& username,
                              const std::string& password) {
  auto client = makeClientFor(url);
  HTTPClient http;

  LOG_DBG("HTTP", "Fetching: %s", url.c_str());

  http.begin(*client, url.c_str());
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  applyAuthAndHeaders(http, username, password, {});

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    LOG_ERR("HTTP", "Fetch failed: %d", httpCode);
    http.end();
    return false;
  }

  http.writeToStream(&outContent);

  http.end();

  LOG_DBG("HTTP", "Fetch success");
  return true;
}

bool HttpDownloader::request(const std::string& url, const Request& req, std::string& outContent) {
  auto client = makeClientFor(url);
  HTTPClient http;

  LOG_DBG("HTTP", "%s: %s", req.method.c_str(), url.c_str());

  http.begin(*client, url.c_str());
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  applyAuthAndHeaders(http, req.username, req.password, req.headers);
  if (!req.contentType.empty()) {
    http.addHeader("Content-Type", String(req.contentType.c_str()));
  }

  int httpCode = 0;
  if (req.method == "POST") {
    httpCode = http.POST(req.body.c_str());
  } else if (req.method == "PATCH") {
#ifdef SIMULATOR
    // Simulator's HTTPClient stub has no sendRequest; treat PATCH as a no-op success.
    httpCode = HTTP_CODE_OK;
#else
    // Arduino HTTPClient has no first-class PATCH; sendRequest handles arbitrary verbs.
    httpCode = http.sendRequest(req.method.c_str(), reinterpret_cast<uint8_t*>(const_cast<char*>(req.body.data())),
                                req.body.size());
#endif
  } else {
    httpCode = http.GET();
  }

  if (req.outHttpCode) *req.outHttpCode = httpCode;

  outContent = std::string(http.getString().c_str());
  http.end();

  // Treat 2xx as success without depending on the named HTTP_CODE_* constants,
  // which are partial in the simulator's HTTPClient stub.
  if (httpCode < 200 || httpCode >= 300) {
    LOG_ERR("HTTP", "Request failed: %d", httpCode);
    return false;
  }
  return true;
}

bool HttpDownloader::fetchUrl(const std::string& url, std::string& outContent, const std::string& username,
                              const std::string& password) {
  StreamString stream;
  if (!fetchUrl(url, stream, username, password)) {
    return false;
  }
  outContent = stream.c_str();
  return true;
}

static HttpDownloader::DownloadError downloadToFileImpl(const std::string& url, const std::string& destPath,
                                                        HttpDownloader::ProgressCallback progress,
                                                        const std::string& username, const std::string& password,
                                                        const HttpDownloader::HeaderList& headers) {
  auto client = makeClientFor(url);
  HTTPClient http;

  LOG_DBG("HTTP", "Downloading: %s", url.c_str());
  LOG_DBG("HTTP", "Destination: %s", destPath.c_str());

  http.begin(*client, url.c_str());
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  applyAuthAndHeaders(http, username, password, headers);

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    LOG_ERR("HTTP", "Download failed: %d", httpCode);
    http.end();
    return HttpDownloader::HTTP_ERROR;
  }

  const int64_t reportedLength = http.getSize();
  const size_t contentLength = reportedLength > 0 ? static_cast<size_t>(reportedLength) : 0;
  if (contentLength > 0) {
    LOG_DBG("HTTP", "Content-Length: %zu", contentLength);
  } else {
    LOG_DBG("HTTP", "Content-Length: unknown");
  }

  // Remove existing file if present
  if (Storage.exists(destPath.c_str())) {
    Storage.remove(destPath.c_str());
  }

  // Open file for writing
  FsFile file;
  if (!Storage.openFileForWrite("HTTP", destPath.c_str(), file)) {
    LOG_ERR("HTTP", "Failed to open file for writing");
    http.end();
    return HttpDownloader::FILE_ERROR;
  }

  // Let HTTPClient handle chunked decoding and stream body bytes into the file.
  FileWriteStream fileStream(file, contentLength, progress);
  const int writeResult = http.writeToStream(&fileStream);

  file.close();
  http.end();

  if (writeResult < 0) {
    LOG_ERR("HTTP", "writeToStream error: %d", writeResult);
    Storage.remove(destPath.c_str());
    return HttpDownloader::HTTP_ERROR;
  }

  const size_t downloaded = fileStream.downloaded();
  LOG_DBG("HTTP", "Downloaded %zu bytes", downloaded);

  // Guard against partial writes even if HTTPClient completes.
  if (!fileStream.ok()) {
    LOG_ERR("HTTP", "Write failed during download");
    Storage.remove(destPath.c_str());
    return HttpDownloader::FILE_ERROR;
  }

  if (contentLength == 0 && downloaded == 0) {
    LOG_ERR("HTTP", "Download failed: no data received");
    Storage.remove(destPath.c_str());
    return HttpDownloader::HTTP_ERROR;
  }

  // Verify download size if known
  if (contentLength > 0 && downloaded != contentLength) {
    LOG_ERR("HTTP", "Size mismatch: got %zu, expected %zu", downloaded, contentLength);
    Storage.remove(destPath.c_str());
    return HttpDownloader::HTTP_ERROR;
  }

  return HttpDownloader::OK;
}

HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& destPath,
                                                             ProgressCallback progress, const std::string& username,
                                                             const std::string& password) {
  return downloadToFileImpl(url, destPath, std::move(progress), username, password, {});
}

HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& destPath,
                                                             const HeaderList& headers, ProgressCallback progress) {
  return downloadToFileImpl(url, destPath, std::move(progress), {}, {}, headers);
}
