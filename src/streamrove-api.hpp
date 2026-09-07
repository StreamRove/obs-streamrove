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

#pragma once

#include <string>

namespace streamrove {

/**
 * One HTTP exchange with the StreamRove API.
 *
 * `error` is a transport failure (DNS, TLS, timeout) and means no reply was
 * received; `status`/`body` describe a reply, which may itself be a refusal.
 */
struct HttpResult {
	long status = 0;
	std::string body;
	std::string error;

	bool ok() const { return error.empty() && status >= 200 && status < 300; }

	/** What to show a person: the API's own {"error": "..."} when there is one. */
	std::string describe() const;
};

/**
 * Thin, blocking client for https://<site>/api. Call it off the UI thread.
 *
 * The API key is a StreamRove developer key ("mux_…"), sent as a Bearer token.
 * It authenticates as the person who created it, so the plugin sees exactly
 * the streams that person sees in the dashboard.
 */
class ApiClient {
public:
	ApiClient(std::string baseUrl, std::string apiKey);

	HttpResult get(const std::string &path) const;
	HttpResult post(const std::string &path, const std::string &jsonBody) const;
	HttpResult patch(const std::string &path, const std::string &jsonBody) const;

	/** Site origin without a trailing slash or "/api" suffix. */
	const std::string &baseUrl() const { return baseUrl_; }

	/** "streamrove.com/", "https://streamrove.com/api" → "https://streamrove.com". */
	static std::string normalizeBaseUrl(std::string url);

	/** Requests executing right now on any worker thread, across all clients. */
	static int inFlight();

private:
	HttpResult request(const char *method, const std::string &path, const std::string *jsonBody) const;

	std::string baseUrl_;
	std::string apiKey_;
};

} // namespace streamrove
