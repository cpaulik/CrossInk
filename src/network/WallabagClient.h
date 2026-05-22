#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "HttpDownloader.h"
#include "WallabagServerStore.h"

struct WallabagArticle {
  int64_t id = 0;
  std::string title;
  std::string domain;  // domain_name from API (used as subtitle)
  int readingTime = 0;
};

/**
 * Wallabag REST client.
 * Performs an OAuth2 password-grant login on first use and keeps the access
 * token cached in-memory for the lifetime of the instance. Tokens are not
 * persisted to disk because they expire after ~3600s and re-auth is cheap.
 */
class WallabagClient {
 public:
  enum class Error {
    OK,
    AuthFailed,
    NetworkError,
    ParseError,
    DownloadFailed,
  };

  explicit WallabagClient(WallabagServer server) : server_(std::move(server)) {}

  /**
   * List unread (archive=0) articles. Returns up to perPage entries on the
   * given page (Wallabag pagination is 1-indexed).
   * outHasMore is set when the response indicates more pages are available.
   */
  Error listUnread(int page, int perPage, std::vector<WallabagArticle>& outArticles, bool& outHasMore);

  /**
   * Export an article as EPUB to destPath.
   */
  Error exportEpub(int64_t articleId, const std::string& destPath, HttpDownloader::ProgressCallback progress);

  /**
   * Archive (mark as read) an article.
   */
  Error archive(int64_t articleId);

 private:
  WallabagServer server_;
  std::string accessToken_;
  std::string refreshToken_;

  Error ensureToken();
  Error fetchToken(const std::string& grantType);
  HttpDownloader::HeaderList authHeaders() const;
};
