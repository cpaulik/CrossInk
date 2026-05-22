#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Activity showing the list of configured Wallabag servers.
 * Same shape as OpdsServerListActivity: settings mode allows add/edit/delete,
 * picker mode (used from the home screen) opens the article browser directly.
 */
class WallabagServerListActivity final : public Activity {
 public:
  explicit WallabagServerListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool pickerMode = false)
      : Activity("WallabagServerList", renderer, mappedInput), pickerMode(pickerMode) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  bool pickerMode = false;

  int getItemCount() const;
  void handleSelection();
};
