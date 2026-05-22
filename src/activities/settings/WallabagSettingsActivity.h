#pragma once

#include "WallabagServerStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Edit screen for a single Wallabag server.
 * Shows Name, URL, Client ID, Client Secret, Username, Password fields and a Delete option.
 */
class WallabagSettingsActivity final : public Activity {
 public:
  /**
   * @param serverIndex Index into WallabagServerStore, or -1 for a new server
   */
  explicit WallabagSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int serverIndex = -1)
      : Activity("WallabagSettings", renderer, mappedInput), serverIndex(serverIndex) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;

  size_t selectedIndex = 0;
  int serverIndex;
  WallabagServer editServer;
  bool isNewServer = false;
  bool showSaveError = false;

  int getMenuItemCount() const;
  void handleSelection();
  bool saveServer();
};
