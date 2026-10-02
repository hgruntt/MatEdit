#include "Contributors.h"
#include "Config.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

namespace {
constexpr std::size_t kMaxListBytes = 256 * 1024;

std::mutex g_contributorsMutex;
std::vector<std::string> g_contributors;
std::thread g_refreshThread;
bool g_refreshStarted = false;

fs::path GetExecutableDirectory() {
#ifdef _WIN32
    std::array<wchar_t, 4096> buffer{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length > 0 && length < buffer.size()) return fs::path(buffer.data()).parent_path();
#else
    std::error_code ec;
    const fs::path executable = fs::read_symlink("/proc/self/exe", ec);
    if (!ec && !executable.empty()) return executable.parent_path();
#endif
    return fs::current_path();
}

bool IsValidUsername(const std::string& username) {
    if (username.empty() || username.size() > 39 || username.front() == '-' || username.back() == '-') return false;
    return std::all_of(username.begin(), username.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '-';
    });
}

std::vector<std::string> ParseList(const std::string& contents) {
    std::vector<std::string> usernames;
    std::size_t start = 0;
    while (start < contents.size() && usernames.size() < 100) {
        const std::size_t end = contents.find('\n', start);
        std::string username = contents.substr(start, end == std::string::npos ? end : end - start);
        if (start == 0 && username.size() >= 3 &&
            static_cast<unsigned char>(username[0]) == 0xEF &&
            static_cast<unsigned char>(username[1]) == 0xBB &&
            static_cast<unsigned char>(username[2]) == 0xBF) {
            username.erase(0, 3);
        }
        while (!username.empty() && std::isspace(static_cast<unsigned char>(username.back()))) username.pop_back();
        const auto first = std::find_if_not(username.begin(), username.end(), [](unsigned char c) { return std::isspace(c); });
        username.erase(username.begin(), first);
        if (!username.empty() && username.front() != '#' && IsValidUsername(username)) usernames.push_back(std::move(username));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return usernames;
}

std::vector<std::string> ReadListFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    std::string contents(kMaxListBytes, '\0');
    file.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    contents.resize(static_cast<std::size_t>(file.gcount()));
    return ParseList(contents);
}

bool SaveCache(const std::vector<std::string>& usernames) {
    const fs::path cachePath = GetConfigDirectory() / "contributors_cache.txt";
    std::error_code directoryError;
    fs::create_directories(cachePath.parent_path(), directoryError);
    if (directoryError) {
        std::fprintf(stderr, "MatEdit: could not create contributor cache directory '%s': %s\n",
                     cachePath.parent_path().string().c_str(), directoryError.message().c_str());
        return false;
    }
    const fs::path temporaryPath = cachePath.string() + ".tmp";
    std::ofstream file(temporaryPath, std::ios::binary | std::ios::trunc);
    if (!file) {
        std::fprintf(stderr, "MatEdit: could not open contributor cache '%s' for writing\n", temporaryPath.string().c_str());
        return false;
    }
    for (const auto& username : usernames) file << username << '\n';
    file.close();
    if (!file) {
        std::fprintf(stderr, "MatEdit: could not write contributor cache '%s'\n", temporaryPath.string().c_str());
        std::error_code ec;
        fs::remove(temporaryPath, ec);
        return false;
    }
#ifdef _WIN32
    if (!MoveFileExW(temporaryPath.c_str(), cachePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::fprintf(stderr, "MatEdit: could not replace contributor cache '%s' (Windows error %lu)\n",
                     cachePath.string().c_str(), static_cast<unsigned long>(GetLastError()));
        std::error_code ec;
        fs::remove(temporaryPath, ec);
        return false;
    }
#else
    std::error_code ec;
    fs::rename(temporaryPath, cachePath, ec);
    if (ec) {
        std::fprintf(stderr, "MatEdit: could not replace contributor cache '%s': %s\n",
                     cachePath.string().c_str(), ec.message().c_str());
        fs::remove(temporaryPath, ec);
        return false;
    }
#endif
    return true;
}

#ifdef _WIN32
std::string DownloadUrl(const wchar_t* host, const wchar_t* path, bool isApi) {
    HINTERNET session = WinHttpOpen(L"MatEdit", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return {};
    WinHttpSetTimeouts(session, 2000, 2000, 3000, 4000);
    HINTERNET connection = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", path, nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;

    std::string contents;
    const wchar_t* headers = isApi ? L"Accept: application/vnd.github+json\r\n" : WINHTTP_NO_ADDITIONAL_HEADERS;
    const DWORD headerLength = isApi ? static_cast<DWORD>(-1) : 0;
    if (request && WinHttpSendRequest(request, headers, headerLength,
                                      WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                                WINHTTP_NO_HEADER_INDEX) && status == 200) {
            while (contents.size() < kMaxListBytes) {
                std::array<char, 2048> buffer{};
                DWORD bytesRead = 0;
                if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead) || bytesRead == 0) break;
                const std::size_t count = std::min<std::size_t>(bytesRead, kMaxListBytes - contents.size());
                contents.append(buffer.data(), count);
            }
        }
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return contents;
}

std::string DownloadCustomList() {
    return DownloadUrl(L"raw.githubusercontent.com", L"/hgruntt/MatEdit/main/CONTRIBUTORS.txt", false);
}

std::string DownloadApiList() {
    return DownloadUrl(L"api.github.com", L"/repos/hgruntt/MatEdit/contributors?per_page=100", true);
}
#else
std::string DownloadUrl(const char* url) {
    const std::string command = std::string("curl --fail --silent --location --max-time 5 --user-agent MatEdit '") + url + "'";
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return {};
    std::string contents;
    std::array<char, 2048> buffer{};
    while (contents.size() < kMaxListBytes && std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        const std::size_t count = std::min<std::size_t>(std::char_traits<char>::length(buffer.data()), kMaxListBytes - contents.size());
        contents.append(buffer.data(), count);
    }
    const int result = pclose(pipe);
    if (result == -1 || !WIFEXITED(result) || WEXITSTATUS(result) != 0) return {};
    return contents;
}

std::string DownloadCustomList() {
    return DownloadUrl("https://raw.githubusercontent.com/hgruntt/MatEdit/main/CONTRIBUTORS.txt");
}

std::string DownloadApiList() {
    return DownloadUrl("https://api.github.com/repos/hgruntt/MatEdit/contributors?per_page=100");
}
#endif

std::vector<std::string> ParseApiList(const std::string& contents) {
    static const std::regex loginPattern(R"re("login"\s*:\s*"([A-Za-z0-9-]+)")re");
    std::vector<std::string> usernames;
    for (std::sregex_iterator it(contents.begin(), contents.end(), loginPattern), end; it != end && usernames.size() < 100; ++it) {
        std::string username = (*it)[1].str();
        if (IsValidUsername(username) && std::find(usernames.begin(), usernames.end(), username) == usernames.end()) {
            usernames.push_back(std::move(username));
        }
    }
    return usernames;
}

std::vector<std::string> DownloadContributors() {
    std::vector<std::string> usernames = ParseList(DownloadCustomList());
    if (!usernames.empty()) return usernames;
    return ParseApiList(DownloadApiList());
}

void RefreshInBackground() {
    const std::vector<std::string> updated = DownloadContributors();
    if (updated.empty() || !SaveCache(updated)) return;
    std::lock_guard<std::mutex> lock(g_contributorsMutex);
    g_contributors = updated;
}
}

void StartContributorRefresh() {
    if (g_refreshStarted) return;
    g_refreshStarted = true;

    const fs::path executableDirectory = GetExecutableDirectory();
    std::vector<std::string> initial = ReadListFile(GetConfigDirectory() / "contributors_cache.txt");
    if (initial.empty()) initial = ReadListFile(executableDirectory / "contributors_cache.txt");
    if (initial.empty()) initial = ReadListFile(executableDirectory / "CONTRIBUTORS.txt");
    {
        std::lock_guard<std::mutex> lock(g_contributorsMutex);
        g_contributors = std::move(initial);
    }

    g_refreshThread = std::thread(RefreshInBackground);
}

void StopContributorRefresh() {
    if (g_refreshThread.joinable()) g_refreshThread.join();
    g_refreshStarted = false;
}

std::vector<std::string> GetContributors() {
    std::lock_guard<std::mutex> lock(g_contributorsMutex);
    return g_contributors;
}