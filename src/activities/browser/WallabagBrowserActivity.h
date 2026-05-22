#pragma once
#include <string>
#include <utility>
#include <vector>

#include "WallabagServerStore.h"
#include "activities/Activity.h"
#include "network/WallabagClient.h"
#include "util/ButtonNavigator.h"

/**
 * Activity for browsing and reading unread Wallabag articles.
 * Lists unread articles (paginated), and on confirmation downloads the
 * selected article as an EPUB into /wallabag/, archives it in Wallabag,
 * and immediately opens the reader on the downloaded file.
 */
class WallabagBrowserActivity final : public Activity {
 public:
  enum class State { CHECK_WIFI, WIFI_SELECTION, LOADING, BROWSING, DOWNLOADING, ERROR };

  explicit WallabagBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, WallabagServer serverArg)
      : Activity("WallabagBrowser", renderer, mappedInput), server(std::move(serverArg)), client(server) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  State state = State::LOADING;
  std::vector<WallabagArticle> articles;
  int selectorIndex = 0;
  int currentPage = 1;
  bool hasMorePages = false;
  std::string statusMessage;
  std::string errorMessage;
  size_t downloadProgress = 0;
  size_t downloadTotal = 0;

  WallabagServer server;
  WallabagClient client;

  void checkAndConnectWifi();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void fetchArticles();
  void openArticle(const WallabagArticle& article);
  bool preventAutoSleep() override { return true; }
};
