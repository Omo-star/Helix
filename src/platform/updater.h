#pragma once
//
// updater.h — auto-update check + download for Helix.
//
// On startup:
//   1. applyPendingUpdate() swaps a previously-downloaded update into place.
//   2. checkForUpdateAsync() runs in a background thread:
//      - Fetches the latest release tag from the GitHub API.
//      - If newer than HELIX_VERSION, downloads the platform binary and saves
//        it next to the running executable as "Helix-update" (+ extension).
//      - Sets updateAvailable / updateVersion so the shell can show a banner.
//      - The update is applied on the NEXT launch by applyPendingUpdate().
//
// On Windows, a running .exe can be renamed (but not overwritten), so:
//   startup: rename Helix-update.exe -> Helix.exe (after renaming running exe out of the way)
//   On Linux/macOS the binary isn't locked, so a simple rename works.
//

#include "platform/version.h"
#include "network/fetcher.h"
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <cstdio>
#include <cstring>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

struct Updater {
    std::atomic<bool> updateAvailable{false};
    std::atomic<bool> checking{false};
    std::string updateVersion;
    std::string statusMessage;
    std::function<void()> onStatusChanged;

    // Call on startup BEFORE showing the window. Swaps a staged update binary
    // into place (from a previous session's download).
    static void applyPendingUpdate(const std::string& exePath) {
        std::string updatePath = pendingPath(exePath);
        std::string oldPath = exePath + ".old";

        // Clean up old backup from a previous update.
        std::remove(oldPath.c_str());

        FILE* f = fopen(updatePath.c_str(), "rb");
        if (!f) return;  // no pending update
        fseek(f, 0, SEEK_END);
        long updateSize = ftell(f);
        fclose(f);
        // Sanity: update must be at least 500 KB to be a real binary.
        if (updateSize < 500 * 1024) {
            std::remove(updatePath.c_str());
            return;
        }

#ifdef _WIN32
        // Windows: rename running exe out of the way, move update in.
        if (MoveFileA(exePath.c_str(), oldPath.c_str())) {
            if (!MoveFileA(updatePath.c_str(), exePath.c_str())) {
                // Rollback if the move fails.
                MoveFileA(oldPath.c_str(), exePath.c_str());
            }
        }
#else
        // Linux/macOS: just rename over (binary isn't locked).
        std::rename(updatePath.c_str(), exePath.c_str());
#endif
    }

    // Run in a background thread. Checks GitHub for a newer release and
    // downloads the platform binary if one exists.
    void checkForUpdateAsync(const std::string& exePath) {
        if (checking.exchange(true)) return;  // already running
        std::string exe = exePath;
        std::thread([this, exe]() {
            checkAndDownload(exe);
            checking = false;
        }).detach();
    }

private:
    void setStatus(const std::string& msg) {
        statusMessage = msg;
        if (onStatusChanged) onStatusChanged();
    }

    void checkAndDownload(const std::string& exePath) {
        setStatus("Checking for updates...");

        auto res = FetchUrl("https://api.github.com/repos/Omo-star/Helix/releases/latest");
        if (!res.success || res.body.empty()) {
            setStatus("");
            return;
        }

        // Minimal JSON parsing: find "tag_name": "vX.Y.Z"
        std::string tag;
        {
            size_t pos = res.body.find("\"tag_name\"");
            if (pos == std::string::npos) { setStatus(""); return; }
            size_t q1 = res.body.find('"', pos + 10);
            size_t q2 = res.body.find('"', q1 + 1);
            if (q1 == std::string::npos || q2 == std::string::npos) { setStatus(""); return; }
            tag = res.body.substr(q1 + 1, q2 - q1 - 1);
        }

        // Strip leading 'v' for comparison.
        std::string remote = tag;
        if (!remote.empty() && remote[0] == 'v') remote = remote.substr(1);
        std::string local = HELIX_VERSION;

        if (!isNewer(remote, local)) {
            setStatus("");
            return;
        }

        // Find the download URL for the current platform's asset.
        std::string assetName;
#ifdef _WIN32
        assetName = "Helix-windows.exe";
#elif defined(__APPLE__)
        assetName = "Helix-macos.zip";
#else
        assetName = "Helix-linux";
#endif

        // Find the browser_download_url that contains our asset name.
        std::string downloadUrl;
        {
            std::string needle = "\"browser_download_url\"";
            size_t pos = 0;
            while (pos < res.body.size()) {
                size_t urlKey = res.body.find(needle, pos);
                if (urlKey == std::string::npos) break;
                size_t q1 = res.body.find('"', urlKey + needle.size());
                size_t q2 = (q1 != std::string::npos) ? res.body.find('"', q1 + 1) : std::string::npos;
                if (q1 != std::string::npos && q2 != std::string::npos) {
                    std::string url = res.body.substr(q1 + 1, q2 - q1 - 1);
                    if (url.find(assetName) != std::string::npos) {
                        downloadUrl = url;
                        break;
                    }
                }
                pos = urlKey + needle.size();
            }
            if (downloadUrl.empty()) { setStatus(""); return; }
        }

        setStatus("Downloading Helix " + tag + "...");

        auto dlRes = FetchUrl(downloadUrl);
        if (!dlRes.success || dlRes.body.size() < 500 * 1024) {
            setStatus("Update download failed.");
            return;
        }

        // Write to the staging path.
        std::string updatePath = pendingPath(exePath);
        {
            std::ofstream out(updatePath, std::ios::binary);
            if (!out.is_open()) { setStatus("Failed to write update."); return; }
            out.write(dlRes.body.data(), dlRes.body.size());
        }

#ifndef _WIN32
        // Make the downloaded binary executable on Linux/macOS.
        chmod(updatePath.c_str(), 0755);
#endif

        updateVersion = tag;
        updateAvailable = true;
        setStatus("Helix " + tag + " ready — restart to update.");
    }

    static std::string pendingPath(const std::string& exePath) {
#ifdef _WIN32
        // Replace .exe with -update.exe
        size_t dot = exePath.rfind('.');
        if (dot != std::string::npos)
            return exePath.substr(0, dot) + "-update" + exePath.substr(dot);
        return exePath + "-update";
#else
        return exePath + "-update";
#endif
    }

    // Simple semver comparison: returns true if remote > local.
    static bool isNewer(const std::string& remote, const std::string& local) {
        auto parse = [](const std::string& s) -> std::tuple<int,int,int> {
            int major = 0, minor = 0, patch = 0;
            if (sscanf(s.c_str(), "%d.%d.%d", &major, &minor, &patch) < 1)
                return {0, 0, 0};
            return {major, minor, patch};
        };
        return parse(remote) > parse(local);
    }
};
