#include "WallabagClient.h"

#include <ArduinoJson.h>
#ifdef SIMULATOR
#include <ArduinoJsonStringCompat.h>
#endif
#include <Logging.h>

#include <cstdio>

#include "util/UrlUtils.h"

namespace {
std::string urlEncode(const std::string& s) {
  std::string out;
  out.reserve(s.size() * 3);
  for (unsigned char c : s) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", c);
      out += buf;
    }
  }
  return out;
}
}  // namespace

WallabagClient::Error WallabagClient::ensureToken() {
  if (!accessToken_.empty()) return Error::OK;
  return fetchToken("password");
}

WallabagClient::Error WallabagClient::fetchToken(const std::string& grantType) {
  if (server_.url.empty() || server_.clientId.empty() || server_.clientSecret.empty()) {
    return Error::AuthFailed;
  }

  const std::string url = UrlUtils::buildUrl(server_.url, "/oauth/v2/token");

  // Wallabag's docs show both password-grant and refresh-token flows here.
  std::string body = "grant_type=" + urlEncode(grantType) + "&client_id=" + urlEncode(server_.clientId) +
                     "&client_secret=" + urlEncode(server_.clientSecret);
  if (grantType == "password") {
    body += "&username=" + urlEncode(server_.username) + "&password=" + urlEncode(server_.password);
  } else if (grantType == "refresh_token") {
    body += "&refresh_token=" + urlEncode(refreshToken_);
  }

  HttpDownloader::Request req;
  req.method = "POST";
  req.body = body;
  req.contentType = "application/x-www-form-urlencoded";

  std::string response;
  if (!HttpDownloader::request(url, req, response)) {
    LOG_ERR("WB", "Token request failed");
    return Error::AuthFailed;
  }

  JsonDocument doc;
  if (deserializeJson(doc, response)) {
    LOG_ERR("WB", "Token response not JSON");
    return Error::AuthFailed;
  }

  const char* token = doc["access_token"] | "";
  const char* refresh = doc["refresh_token"] | "";
  if (token[0] == '\0') {
    LOG_ERR("WB", "Token missing in response");
    return Error::AuthFailed;
  }
  accessToken_ = token;
  refreshToken_ = refresh;
  LOG_DBG("WB", "Got access token (%zu chars)", accessToken_.size());
  return Error::OK;
}

HttpDownloader::HeaderList WallabagClient::authHeaders() const {
  return {{"Authorization", "Bearer " + accessToken_}, {"Accept", "application/json"}};
}

WallabagClient::Error WallabagClient::listUnread(int page, int perPage, std::vector<WallabagArticle>& outArticles,
                                                 bool& outHasMore) {
  outArticles.clear();
  outHasMore = false;

  if (auto err = ensureToken(); err != Error::OK) return err;

  char query[96];
  snprintf(query, sizeof(query), "/api/entries.json?archive=0&perPage=%d&page=%d", perPage, page);
  const std::string url = UrlUtils::buildUrl(server_.url, query);

  HttpDownloader::Request req;
  req.method = "GET";
  req.headers = authHeaders();
  int httpCode = 0;
  req.outHttpCode = &httpCode;

  std::string response;
  bool ok = HttpDownloader::request(url, req, response);
  if (!ok && httpCode == 401) {
    // Token expired — try a single refresh and retry once.
    accessToken_.clear();
    if (auto err = ensureToken(); err != Error::OK) return err;
    req.headers = authHeaders();
    httpCode = 0;
    ok = HttpDownloader::request(url, req, response);
  }
  if (!ok) {
    LOG_ERR("WB", "List entries failed (HTTP %d)", httpCode);
    return Error::NetworkError;
  }

  // ArduinoJson 7 sizes its document dynamically; a single big response is
  // fine here because perPage is small (default 30).
  JsonDocument doc;
  if (deserializeJson(doc, response)) {
    LOG_ERR("WB", "List entries: JSON parse failed");
    return Error::ParseError;
  }

  // Newer Wallabag versions return {"_embedded": {"items": [...]}, "page": N, "pages": M}.
  // Older returns are an array directly; handle both.
  JsonArray items;
  if (doc["_embedded"].is<JsonObject>()) {
    items = doc["_embedded"]["items"].as<JsonArray>();
    const int curPage = doc["page"] | 1;
    const int totalPages = doc["pages"] | 1;
    outHasMore = curPage < totalPages;
  } else if (doc.is<JsonArray>()) {
    items = doc.as<JsonArray>();
  }

  for (JsonObject item : items) {
    WallabagArticle a;
    a.id = item["id"] | 0;
    a.title = item["title"] | std::string("");
    a.domain = item["domain_name"] | std::string("");
    a.readingTime = item["reading_time"] | 0;
    if (a.id != 0) {
      outArticles.push_back(std::move(a));
    }
  }
  return Error::OK;
}

WallabagClient::Error WallabagClient::exportEpub(int64_t articleId, const std::string& destPath,
                                                 HttpDownloader::ProgressCallback progress) {
  if (auto err = ensureToken(); err != Error::OK) return err;

  char path[64];
  snprintf(path, sizeof(path), "/api/entries/%lld/export.epub", static_cast<long long>(articleId));
  const std::string url = UrlUtils::buildUrl(server_.url, path);

  const auto result = HttpDownloader::downloadToFile(url, destPath, authHeaders(), std::move(progress));
  if (result != HttpDownloader::OK) {
    LOG_ERR("WB", "EPUB export failed (err=%d)", result);
    return Error::DownloadFailed;
  }
  return Error::OK;
}

WallabagClient::Error WallabagClient::archive(int64_t articleId) {
  if (auto err = ensureToken(); err != Error::OK) return err;

  char path[64];
  snprintf(path, sizeof(path), "/api/entries/%lld.json", static_cast<long long>(articleId));
  const std::string url = UrlUtils::buildUrl(server_.url, path);

  HttpDownloader::Request req;
  req.method = "PATCH";
  req.body = "{\"archive\":1}";
  req.contentType = "application/json";
  req.headers = authHeaders();
  int httpCode = 0;
  req.outHttpCode = &httpCode;

  std::string response;
  if (!HttpDownloader::request(url, req, response)) {
    LOG_ERR("WB", "Archive failed (HTTP %d)", httpCode);
    return Error::NetworkError;
  }
  return Error::OK;
}
