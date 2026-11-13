// Is there a newer release? The latest release on GitHub (its version tag, from api.github.com's public "latest
// release" page: no account, nothing about the player sent), read in the background. Checked at most once a day
// (Options > Updates > Check for Updates); the launcher shows a badge when there's a newer version. Nothing is
// downloaded or installed: the badge only opens the release's page.
#pragma once
#include <atomic>
#include <string>
#include <thread>

// The GitHub repository ("owner/name") whose releases are checked.
constexpr const char* kUpdateRepo = "Darhkfox/AladdinCollection";

class UpdateCheck {
public:
    static std::string api_url() { return std::string("https://api.github.com/repos/") + kUpdateRepo + "/releases/latest"; }
    static std::string page_for(const std::string& tag) {
        return std::string("https://github.com/") + kUpdateRepo + "/releases/tag/" + tag;
    }

    ~UpdateCheck();
    void start();                  // (in the background; nothing if one is running)
    bool running() const { return state_ == 1; }
    // Once, when a check has finished: true and the latest release's tag ("v1.3"), or true and "" when it couldn't
    // be read (offline, ...).
    bool poll(std::string* tag);

    // The "tag_name" of a GitHub release's JSON ("" if there's none).
    static std::string tag_from_json(const std::string& body);
    // Is the tag ("v1.3", "1.2.1") a later version than `current` ("1.0")?
    static bool is_newer(const std::string& tag, const std::string& current);

private:
    std::thread thread_;
    std::atomic<int> state_{0};    // 0 idle, 1 running, 2 done
    std::string tag_;
};
