#include "WallabagSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "WallabagServerStore.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Editable fields: Name, URL, Client ID, Client Secret, Username, Password.
constexpr int BASE_ITEMS = 6;
}  // namespace

int WallabagSettingsActivity::getMenuItemCount() const {
  return isNewServer ? BASE_ITEMS : BASE_ITEMS + 1;  // +1 for Delete
}

void WallabagSettingsActivity::onEnter() {
  Activity::onEnter();

  selectedIndex = 0;
  isNewServer = (serverIndex < 0);
  showSaveError = false;

  if (!isNewServer) {
    const auto* server = WALLABAG_STORE.getServer(static_cast<size_t>(serverIndex));
    if (server) {
      editServer = *server;
    } else {
      isNewServer = true;
      serverIndex = -1;
    }
  }

  requestUpdate();
}

void WallabagSettingsActivity::onExit() { Activity::onExit(); }

void WallabagSettingsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  const int menuItems = getMenuItemCount();
  buttonNavigator.onNext([this, menuItems] {
    selectedIndex = (selectedIndex + 1) % menuItems;
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuItems] {
    selectedIndex = (selectedIndex + menuItems - 1) % menuItems;
    requestUpdate();
  });
}

bool WallabagSettingsActivity::saveServer() {
  bool success = false;

  if (isNewServer) {
    success = WALLABAG_STORE.addServer(editServer);
    if (success) {
      isNewServer = false;
      serverIndex = static_cast<int>(WALLABAG_STORE.getCount()) - 1;
    } else {
      LOG_ERR("WB", "Failed to add Wallabag server");
    }
  } else {
    success = WALLABAG_STORE.updateServer(static_cast<size_t>(serverIndex), editServer);
    if (!success) {
      LOG_ERR("WB", "Failed to update Wallabag server at index %d", serverIndex);
    }
  }

  showSaveError = !success;
  if (showSaveError) requestUpdate();
  return success;
}

void WallabagSettingsActivity::handleSelection() {
  // field is a pointer to the std::string member of editServer to be updated.
  // Capturing the pointer by value is safe even after stringField returns, because
  // editServer is a member of *this and lives as long as the activity.
  auto stringField = [this](StrId title, std::string* field, int maxLen, InputType inputType) {
    auto handler = [this, field](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        *field = kb.text;
        saveServer();
        requestUpdate();
      }
    };
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput,
                                                                   I18n::getInstance().get(title), *field, maxLen,
                                                                   inputType),
                           handler);
  };

  if (selectedIndex == 0) {
    stringField(StrId::STR_SERVER_NAME, &editServer.name, 63, InputType::Text);
  } else if (selectedIndex == 1) {
    const std::string prefillUrl = editServer.url.empty() ? "https://" : editServer.url;
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        editServer.url = (kb.text == "https://" || kb.text == "http://") ? "" : kb.text;
        saveServer();
        requestUpdate();
      }
    };
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_WALLABAG_URL),
                                                                   prefillUrl, 127, InputType::Url),
                           handler);
  } else if (selectedIndex == 2) {
    stringField(StrId::STR_WALLABAG_CLIENT_ID, &editServer.clientId, 127, InputType::Text);
  } else if (selectedIndex == 3) {
    stringField(StrId::STR_WALLABAG_CLIENT_SECRET, &editServer.clientSecret, 127, InputType::Password);
  } else if (selectedIndex == 4) {
    stringField(StrId::STR_USERNAME, &editServer.username, 63, InputType::Text);
  } else if (selectedIndex == 5) {
    stringField(StrId::STR_PASSWORD, &editServer.password, 63, InputType::Password);
  } else if (selectedIndex == 6 && !isNewServer) {
    if (!WALLABAG_STORE.removeServer(static_cast<size_t>(serverIndex))) {
      LOG_ERR("WB", "Failed to remove Wallabag server at index %d", serverIndex);
      showSaveError = true;
      requestUpdate();
      return;
    }
    finish();
  }
}

void WallabagSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const char* header = isNewServer ? tr(STR_ADD_SERVER) : tr(STR_WALLABAG);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, header);
  GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                    tr(STR_WALLABAG_URL_HINT));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + metrics.tabBarHeight;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  const int menuItems = getMenuItemCount();

  const StrId fieldNames[] = {StrId::STR_SERVER_NAME, StrId::STR_WALLABAG_URL,    StrId::STR_WALLABAG_CLIENT_ID,
                              StrId::STR_WALLABAG_CLIENT_SECRET, StrId::STR_USERNAME,        StrId::STR_PASSWORD};

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, menuItems, static_cast<int>(selectedIndex),
      [this, &fieldNames](int index) {
        if (index < BASE_ITEMS) {
          return std::string(I18N.get(fieldNames[index]));
        }
        return std::string(tr(STR_DELETE_SERVER));
      },
      nullptr, nullptr,
      [this](int index) {
        const auto secretMask = [](const std::string& s) { return s.empty() ? std::string(tr(STR_NOT_SET)) : std::string("******"); };
        switch (index) {
          case 0:
            return editServer.name.empty() ? std::string(tr(STR_NOT_SET)) : editServer.name;
          case 1:
            return editServer.url.empty() ? std::string(tr(STR_NOT_SET)) : editServer.url;
          case 2:
            return editServer.clientId.empty() ? std::string(tr(STR_NOT_SET)) : editServer.clientId;
          case 3:
            return secretMask(editServer.clientSecret);
          case 4:
            return editServer.username.empty() ? std::string(tr(STR_NOT_SET)) : editServer.username;
          case 5:
            return secretMask(editServer.password);
          default:
            return std::string("");
        }
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (showSaveError) {
    GUI.drawPopup(renderer, tr(STR_ERROR_GENERAL_FAILURE));
  }

  renderer.displayBuffer();
}
