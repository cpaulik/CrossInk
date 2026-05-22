#pragma once
#include <string>
#include <vector>

struct WallabagServer {
  std::string name;
  std::string url;
  std::string clientId;
  std::string clientSecret;
  std::string username;
  std::string password;  // Plaintext in memory; obfuscated with hardware key on disk
};

class WallabagServerStore;
namespace JsonSettingsIO {
bool saveWallabag(const WallabagServerStore& store, const char* path);
bool loadWallabag(WallabagServerStore& store, const char* json, bool* needsResave);
}  // namespace JsonSettingsIO

/**
 * Singleton class for storing Wallabag server configurations on the SD card.
 * Client secret and password are XOR-obfuscated with the device hardware key
 * and base64-encoded before writing to JSON.
 */
class WallabagServerStore {
 private:
  static WallabagServerStore instance;
  std::vector<WallabagServer> servers;

  static constexpr size_t MAX_SERVERS = 8;

  WallabagServerStore() = default;

  friend bool JsonSettingsIO::saveWallabag(const WallabagServerStore&, const char*);
  friend bool JsonSettingsIO::loadWallabag(WallabagServerStore&, const char*, bool*);

 public:
  WallabagServerStore(const WallabagServerStore&) = delete;
  WallabagServerStore& operator=(const WallabagServerStore&) = delete;

  static WallabagServerStore& getInstance() { return instance; }

  bool saveToFile() const;
  bool loadFromFile();

  bool addServer(const WallabagServer& server);
  bool updateServer(size_t index, const WallabagServer& server);
  bool removeServer(size_t index);

  const std::vector<WallabagServer>& getServers() const { return servers; }
  const WallabagServer* getServer(size_t index) const;
  size_t getCount() const { return servers.size(); }
  bool hasServers() const { return !servers.empty(); }
};

#define WALLABAG_STORE WallabagServerStore::getInstance()
