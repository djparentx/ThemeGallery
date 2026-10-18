#include "Net.h"
#include "Log.h"
#include <curl/curl.h>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace {
struct Progress {
    const std::atomic<bool>* cancel;
    std::atomic<uint64_t>* received;
    std::atomic<uint64_t>* total;
};

int onProgress(void* p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
    auto* pr = static_cast<Progress*>(p);
    *pr->received = (uint64_t)dlnow;
    if (dltotal > 0) *pr->total = (uint64_t)dltotal;
    return *pr->cancel ? 1 : 0;  // non-zero aborts the transfer
}

// A new status line means a new response (GitHub redirects to its download server): reset counters
size_t onHeader(char* buf, size_t size, size_t n, void* p) {
    auto* pr = static_cast<Progress*>(p);
    const size_t len = size * n;
    if (len >= 5 && std::strncmp(buf, "HTTP/", 5) == 0) {
        *pr->total = 0;
        *pr->received = 0;
    }
    return len;
}
}  // namespace

namespace Net {

bool checkInternet() {
    CURL* c = curl_easy_init();
    if (!c) return false;
    curl_easy_setopt(c, CURLOPT_URL, "https://github.com");
    curl_easy_setopt(c, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 3L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "ThemeGallery");
    const CURLcode rc = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(c);
    const bool online = rc == CURLE_OK && code > 0;
    if (online) Log::info("net", "online (HTTP " + std::to_string(code) + ")");
    else Log::warn("net", std::string("offline: ") + curl_easy_strerror(rc));
    return online;
}

bool download(const std::string& url, const std::string& path, const std::atomic<bool>& cancel,
              std::atomic<uint64_t>& received, std::atomic<uint64_t>& total, std::string& error) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        error = path + ": " + std::strerror(errno);
        return false;
    }
    CURL* c = curl_easy_init();
    if (!c) {
        std::fclose(f);
        error = "curl init failed";
        return false;
    }
    Progress pr{&cancel, &received, &total};
    char errbuf[CURL_ERROR_SIZE];
    errbuf[0] = '\0';

    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "ThemeGallery");
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);  // under 1 B/s ...
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 60L);  // ... for 60 s = stalled
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, f);  // default write callback is fwrite
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, onHeader);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, &pr);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, onProgress);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &pr);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);

    const CURLcode rc = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(c);
    const bool closed = std::fclose(f) == 0;

    if (rc != CURLE_OK) {
        error = cancel ? "cancelled" : (errbuf[0] ? errbuf : curl_easy_strerror(rc));
        Log::warn("net", url + ": " + error);
        return false;
    }
    if (!closed) {
        error = std::string("write failed: ") + std::strerror(errno);
        return false;
    }
    Log::info("net", "downloaded " + std::to_string(received.load()) + " bytes (HTTP " + std::to_string(code) + ")");
    return true;
}

}  // namespace Net
