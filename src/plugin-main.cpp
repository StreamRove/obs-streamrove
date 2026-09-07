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

#include <QPointer>

#include <chrono>
#include <thread>

#include <curl/curl.h>
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include "streamrove-dock.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

// The dock widget is owned by OBS's main window once it has been added; this
// only observes it, so a frontend event after the window is gone is a no-op.
QPointer<streamrove::Dock> g_dock;

void onFrontendEvent(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_STREAMING_STARTING:
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
	case OBS_FRONTEND_EVENT_STREAMING_STOPPING:
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		if (g_dock) {
			g_dock->streamingStateChanged();
		}
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		if (g_dock) {
			g_dock->shutdown();
		}
		break;
	default:
		break;
	}
}

} // namespace

MODULE_EXPORT const char *obs_module_name(void)
{
	return "StreamRove for OBS";
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Stream to StreamRove and fan out to every connected destination.";
}

bool obs_module_load(void)
{
	curl_global_init(CURL_GLOBAL_DEFAULT);

	auto *dock = new streamrove::Dock();
	if (!obs_frontend_add_dock_by_id("streamrove-dock", obs_module_text("StreamRove.Dock.Title"), dock)) {
		obs_log(LOG_ERROR, "could not add the StreamRove dock");
		delete dock;
		return false;
	}
	g_dock = dock;

	obs_frontend_add_event_callback(onFrontendEvent, nullptr);
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);

	// A request still running on a worker thread must not have libcurl torn
	// down under it. Requests finish within seconds; give them a moment, and
	// if one is somehow still running leave libcurl's global state to process
	// exit rather than crash on the way out.
	for (int i = 0; i < 40 && streamrove::ApiClient::inFlight() > 0; i++) {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	if (streamrove::ApiClient::inFlight() == 0) {
		curl_global_cleanup();
	}
	obs_log(LOG_INFO, "plugin unloaded");
}
