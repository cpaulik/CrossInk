#include "WallabagBrowserActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/StringUtils.h"

namespace {
constexpr int PAGE_ITEMS = 23;
constexpr int PER_PAGE = 30;
constexpr const char* WALLABAG_DIR = "/wallabag";
}  // namespace

void WallabagBrowserActivity::onEnter() {
  Activity::onEnter();

  state = State::CHECK_WIFI;
  articles.clear();
  selectorIndex = 0;
  currentPage = 1;
  hasMorePages = false;
  errorMessage.clear();
  statusMessage = tr(STR_CHECKING_WIFI);
  requestUpdate();

  checkAndConnectWifi();
}

void WallabagBrowserActivity::onExit() {
  Activity::onExit();
  WiFi.mode(WIFI_OFF);
  articles.clear();
}

void WallabagBrowserActivity::loop() {
  if (state == State::WIFI_SELECTION) return;

  if (state == State::ERROR) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        state = State::LOADING;
        statusMessage = tr(STR_LOADING);
        requestUpdate();
        fetchArticles();
      } else {
        launchWifiSelection();
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome();
    }
    return;
  }

  if (state == State::CHECK_WIFI || state == State::LOADING || state == State::DOWNLOADING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome();
    }
    return;
  }

  if (state == State::BROWSING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (!articles.empty()) {
        openArticle(articles[selectorIndex]);
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome();
    }

    if (!articles.empty()) {
      buttonNavigator.onNextRelease([this] {
        selectorIndex = ButtonNavigator::nextIndex(selectorIndex, articles.size());
        requestUpdate();
      });
      buttonNavigator.onPreviousRelease([this] {
        selectorIndex = ButtonNavigator::previousIndex(selectorIndex, articles.size());
        requestUpdate();
      });
      buttonNavigator.onNextContinuous([this] {
        selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, articles.size(), PAGE_ITEMS);
        requestUpdate();
      });
      buttonNavigator.onPreviousContinuous([this] {
        selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, articles.size(), PAGE_ITEMS);
        requestUpdate();
      });
    }
  }
}

void WallabagBrowserActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const char* headerTitle = server.name.empty() ? tr(STR_WALLABAG) : server.name.c_str();
  renderer.drawCenteredText(UI_12_FONT_ID, 15, headerTitle, true, EpdFontFamily::BOLD);

  if (state == State::CHECK_WIFI || state == State::LOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, statusMessage.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == State::ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_ERROR_MSG));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, errorMessage.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == State::DOWNLOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 40, tr(STR_DOWNLOADING));
    auto title = renderer.truncatedText(UI_10_FONT_ID, statusMessage.c_str(), pageWidth - 40);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, title.c_str());
    if (downloadTotal > 0) {
      GUI.drawProgressBar(renderer, Rect{50, pageHeight / 2 + 20, pageWidth - 100, 20}, downloadProgress,
                          downloadTotal);
    }
    renderer.displayBuffer();
    return;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (articles.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_WALLABAG_NO_ARTICLES));
  } else {
    const auto pageStartIndex = selectorIndex / PAGE_ITEMS * PAGE_ITEMS;
    renderer.fillRect(0, 60 + (selectorIndex % PAGE_ITEMS) * 30 - 2, pageWidth - 1, 30);

    for (size_t i = pageStartIndex; i < articles.size() && i < static_cast<size_t>(pageStartIndex + PAGE_ITEMS); i++) {
      const auto& article = articles[i];
      std::string displayText = article.title;
      if (!article.domain.empty()) displayText += " - " + article.domain;
      auto item = renderer.truncatedText(UI_10_FONT_ID, displayText.c_str(), pageWidth - 40);
      renderer.drawText(UI_10_FONT_ID, 20, 60 + (i % PAGE_ITEMS) * 30, item.c_str(),
                        i != static_cast<size_t>(selectorIndex));
    }
  }
  renderer.displayBuffer();
}

void WallabagBrowserActivity::fetchArticles() {
  if (server.url.empty()) {
    state = State::ERROR;
    errorMessage = tr(STR_NO_SERVER_URL);
    requestUpdate();
    return;
  }

  bool hasMore = false;
  std::vector<WallabagArticle> page;
  const auto err = client.listUnread(currentPage, PER_PAGE, page, hasMore);
  if (err != WallabagClient::Error::OK) {
    state = State::ERROR;
    errorMessage = (err == WallabagClient::Error::AuthFailed) ? tr(STR_WALLABAG_AUTH_FAILED) : tr(STR_WALLABAG_LIST_FAILED);
    requestUpdate();
    return;
  }

  articles = std::move(page);
  hasMorePages = hasMore;
  selectorIndex = 0;
  state = State::BROWSING;
  requestUpdate();
}

void WallabagBrowserActivity::openArticle(const WallabagArticle& article) {
  state = State::DOWNLOADING;
  statusMessage = article.title;
  downloadProgress = downloadTotal = 0;
  requestUpdate(true);

  Storage.mkdir(WALLABAG_DIR);
  const std::string filename =
      std::string(WALLABAG_DIR) + "/" + StringUtils::sanitizeFilename(article.title) + ".epub";
  LOG_DBG("WB", "Downloading article %lld -> %s", static_cast<long long>(article.id), filename.c_str());

  const auto err = client.exportEpub(article.id,
                                     filename,
                                     [this](const size_t downloaded, const size_t total) {
                                       downloadProgress = downloaded;
                                       downloadTotal = total;
                                       requestUpdate(true);
                                     });
  if (err != WallabagClient::Error::OK) {
    state = State::ERROR;
    errorMessage = tr(STR_WALLABAG_EXPORT_FAILED);
    requestUpdate();
    return;
  }

  // Archive in Wallabag so it's not shown again on next sync.
  // Don't block the open flow if archiving fails — log and move on.
  if (client.archive(article.id) != WallabagClient::Error::OK) {
    LOG_ERR("WB", "Archive after download failed for article %lld", static_cast<long long>(article.id));
  }

  activityManager.goToReader(filename, true);
}

void WallabagBrowserActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    state = State::LOADING;
    statusMessage = tr(STR_LOADING);
    requestUpdate();
    fetchArticles();
    return;
  }
  launchWifiSelection();
}

void WallabagBrowserActivity::launchWifiSelection() {
  state = State::WIFI_SELECTION;
  requestUpdate();
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void WallabagBrowserActivity::onWifiSelectionComplete(const bool connected) {
  if (connected) {
    state = State::LOADING;
    statusMessage = tr(STR_LOADING);
    requestUpdate(true);
    fetchArticles();
  } else {
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    state = State::ERROR;
    errorMessage = tr(STR_WIFI_CONN_FAILED);
    requestUpdate();
  }
}
