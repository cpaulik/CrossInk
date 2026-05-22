#include "WallabagServerStore.h"

#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>

WallabagServerStore WallabagServerStore::instance;

namespace {
constexpr char WALLABAG_FILE_JSON[] = "/.crosspoint/wallabag.json";
}  // namespace

bool WallabagServerStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveWallabag(*this, WALLABAG_FILE_JSON);
}

bool WallabagServerStore::loadFromFile() {
  if (!Storage.exists(WALLABAG_FILE_JSON)) {
    return false;
  }
  String json = Storage.readFile(WALLABAG_FILE_JSON);
  if (json.isEmpty()) {
    return false;
  }
  bool resave = false;
  bool result = JsonSettingsIO::loadWallabag(*this, json.c_str(), &resave);
  if (result && resave) {
    LOG_DBG("WB", "Resaving JSON with obfuscated secrets");
    saveToFile();
  }
  return result;
}

bool WallabagServerStore::addServer(const WallabagServer& server) {
  if (servers.size() >= MAX_SERVERS) {
    LOG_DBG("WB", "Cannot add more servers, limit of %zu reached", MAX_SERVERS);
    return false;
  }
  servers.push_back(server);
  LOG_DBG("WB", "Added server: %s", server.name.c_str());
  return saveToFile();
}

bool WallabagServerStore::updateServer(size_t index, const WallabagServer& server) {
  if (index >= servers.size()) {
    return false;
  }
  servers[index] = server;
  LOG_DBG("WB", "Updated server: %s", server.name.c_str());
  return saveToFile();
}

bool WallabagServerStore::removeServer(size_t index) {
  if (index >= servers.size()) {
    return false;
  }
  LOG_DBG("WB", "Removed server: %s", servers[index].name.c_str());
  servers.erase(servers.begin() + static_cast<ptrdiff_t>(index));
  return saveToFile();
}

const WallabagServer* WallabagServerStore::getServer(size_t index) const {
  if (index >= servers.size()) {
    return nullptr;
  }
  return &servers[index];
}
