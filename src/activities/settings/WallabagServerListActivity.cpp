#include "WallabagServerListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "WallabagServerStore.h"
#include "WallabagSettingsActivity.h"
#include "activities/ActivityManager.h"
#include "activities/browser/WallabagBrowserActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

int WallabagServerListActivity::getItemCount() const {
  int count = static_cast<int>(WALLABAG_STORE.getCount());
  if (!pickerMode) count++;
  return count;
}

void WallabagServerListActivity::onEnter() {
  Activity::onEnter();
  WALLABAG_STORE.loadFromFile();
  selectedIndex = 0;
  requestUpdate();
}

void WallabagServerListActivity::onExit() { Activity::onExit(); }

void WallabagServerListActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (pickerMode) {
      activityManager.goHome();
    } else {
      finish();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  const int itemCount = getItemCount();
  if (itemCount > 0) {
    buttonNavigator.onNext([this, itemCount] {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, itemCount);
      requestUpdate();
    });
    buttonNavigator.onPrevious([this, itemCount] {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, itemCount);
      requestUpdate();
    });
  }
}

void WallabagServerListActivity::handleSelection() {
  const auto serverCount = static_cast<int>(WALLABAG_STORE.getCount());

  if (pickerMode) {
    if (selectedIndex < serverCount) {
      const auto* server = WALLABAG_STORE.getServer(static_cast<size_t>(selectedIndex));
      if (server) {
        activityManager.replaceActivity(std::make_unique<WallabagBrowserActivity>(renderer, mappedInput, *server));
      }
    }
    return;
  }

  auto resultHandler = [this](const ActivityResult&) {
    WALLABAG_STORE.loadFromFile();
    selectedIndex = 0;
  };

  if (selectedIndex < serverCount) {
    startActivityForResult(std::make_unique<WallabagSettingsActivity>(renderer, mappedInput, selectedIndex),
                           resultHandler);
  } else {
    startActivityForResult(std::make_unique<WallabagSettingsActivity>(renderer, mappedInput, -1), resultHandler);
  }
}

void WallabagServerListActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_WALLABAG_SERVERS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  const int itemCount = getItemCount();

  if (itemCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_WALLABAG_NO_SERVERS));
  } else {
    const auto& servers = WALLABAG_STORE.getServers();
    const auto serverCount = static_cast<int>(servers.size());

    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, itemCount, selectedIndex,
        [&servers, serverCount](int index) {
          if (index < serverCount) {
            const auto& server = servers[index];
            return server.name.empty() ? server.url : server.name;
          }
          return std::string(I18n::getInstance().get(StrId::STR_ADD_SERVER));
        },
        [&servers, serverCount](int index) {
          if (index < serverCount && !servers[index].name.empty()) {
            return servers[index].url;
          }
          return std::string("");
        });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
