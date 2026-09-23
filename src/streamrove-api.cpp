/*
StreamRove for OBS
Copyright (C) 2026 StreamRove

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "streamrove-api.hpp"

#include <atomic>
#include <cctype>
#include <utility>

#include <curl/curl.h>
#include <obs-data.h>
#include <util/base.h>
#include <plugin-support.h>

namespace streamrove {

namespace {

size_t appendBody(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	auto *out = static_cast<std::string *>(userdata);
	out->append(ptr, size * nmemb);
	return size * nmemb;
}

// Module unload must not tear libcurl down under a request that is still
// running on a worker thread; this is how it knows whether one is.
std::atomic<int> g_inFlight{0};

struct InFlight {
	InFlight() { g_inFlight.fetch_add(1); }
	~InFlight() { g_inFlight.fetch_sub(1); }
};

bool startsWithNoCase(const std::string &s, const char *prefix)
{
	for (size_t i = 0; prefix[i] != '\0'; ++i) {
		if (i >= s.size() || std::tolower(static_cast<unsigned char>(s[i])) != prefix[i]) {
			return false;
		}
	}
	return true;
}

/**
 * Whether an address after "http://" names this machine.
 *
 * Deliberately narrow. Userinfo is refused outright, because in
 * "localhost:80@example.com" the host is example.com; and "127." counts only
 * as a numeric address, because 127.example.com is somebody's DNS name.
 */
bool isLoopback(const std::string &rest)
{
	const std::string authority = rest.substr(0, rest.find_first_of("/?#"));
	if (authority.find('@') != std::string::npos) {
		return false;
	}
	std::string host;
	if (!authority.empty() && authority.front() == '[') {
		host = authority.substr(0, authority.find(']') + 1);
	} else {
		host = authority.substr(0, authority.find(':'));
	}
	for (char &c : host) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	if (host == "localhost" || host == "[::1]") {
		return true;
	}
	if (host.rfind("127.", 0) != 0) {
		return false;
	}
	for (char c : host) {
		if (c != '.' && !std::isdigit(static_cast<unsigned char>(c))) {
			return false;
		}
	}
	return true;
}

} // namespace

int ApiClient::inFlight()
{
	return g_inFlight.load();
}

std::string HttpResult::describe() const
{
	if (!error.empty()) {
		return error;
	}
	if (!body.empty()) {
		obs_data_t *data = obs_data_create_from_json(body.c_str());
		if (data) {
			const char *msg = obs_data_get_string(data, "error");
			std::string text = msg ? msg : "";
			obs_data_release(data);
			if (!text.empty()) {
				return text;
			}
		}
	}
	switch (status) {
	case 401:
		return "The API key was not accepted.";
	case 403:
		return "This key is not allowed to do that.";
	case 404:
		return "Not found.";
	default:
		return "HTTP " + std::to_string(status);
	}
}

std::string ApiClient::normalizeBaseUrl(std::string url)
{
	while (!url.empty() && std::isspace(static_cast<unsigned char>(url.back()))) {
		url.pop_back();
	}
	while (!url.empty() && std::isspace(static_cast<unsigned char>(url.front()))) {
		url.erase(url.begin());
	}
	while (!url.empty() && url.back() == '/') {
		url.pop_back();
	}
	if (url.size() >= 4 && url.compare(url.size() - 4, 4, "/api") == 0) {
		url.erase(url.size() - 4);
	}
	if (url.empty()) {
		url = "https://streamrove.com";
	}
	// The key rides in a header on every call, so a server typed as http://
	// used to receive it in the clear before any redirect could help — and
	// redirects are not followed anyway. Plain http is kept only for a server
	// on this machine, which is a developer's; anything else is upgraded.
	if (startsWithNoCase(url, "https://")) {
		url = "https://" + url.substr(8);
	} else if (startsWithNoCase(url, "http://")) {
		const std::string rest = url.substr(7);
		url = (isLoopback(rest) ? "http://" : "https://") + rest;
	} else {
		url = "https://" + url;
	}
	return url;
}

ApiClient::ApiClient(std::string baseUrl, std::string apiKey)
	: baseUrl_(normalizeBaseUrl(std::move(baseUrl))),
	  apiKey_(std::move(apiKey))
{
}

HttpResult ApiClient::get(const std::string &path) const
{
	return request("GET", path, nullptr);
}

HttpResult ApiClient::post(const std::string &path, const std::string &jsonBody) const
{
	return request("POST", path, &jsonBody);
}

HttpResult ApiClient::patch(const std::string &path, const std::string &jsonBody) const
{
	return request("PATCH", path, &jsonBody);
}

HttpResult ApiClient::request(const char *method, const std::string &path, const std::string *jsonBody) const
{
	HttpResult result;
	const InFlight inFlight;

	CURL *curl = curl_easy_init();
	if (!curl) {
		result.error = "Could not initialise HTTP.";
		return result;
	}

	const std::string url = baseUrl_ + "/api" + path;
	const std::string userAgent = std::string("obs-streamrove/") + PLUGIN_VERSION;

	struct curl_slist *headers = nullptr;
	if (!apiKey_.empty()) {
		const std::string auth = "Authorization: Bearer " + apiKey_;
		headers = curl_slist_append(headers, auth.c_str());
	}
	headers = curl_slist_append(headers, "Accept: application/json");
	headers = curl_slist_append(headers, "Content-Type: application/json");

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent.c_str());
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendBody);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.body);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	// The key travels in a header. A redirect would carry it to wherever the
	// redirect points, so none are followed: the API answers in place.
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
#if CURL_AT_LEAST_VERSION(7, 85, 0)
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
#else
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
	if (jsonBody) {
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody->c_str());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(jsonBody->size()));
	}

	const CURLcode code = curl_easy_perform(curl);
	if (code != CURLE_OK) {
		result.error = curl_easy_strerror(code);
	} else {
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
	}

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if (!result.error.empty()) {
		obs_log(LOG_WARNING, "%s %s failed: %s", method, path.c_str(), result.error.c_str());
	} else if (!result.ok()) {
		obs_log(LOG_INFO, "%s %s -> HTTP %ld", method, path.c_str(), result.status);
	}
	return result;
}

} // namespace streamrove
