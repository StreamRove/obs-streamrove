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

#include "streamrove-settings.hpp"

#include <obs-module.h>
#include <util/bmem.h>
#include <util/platform.h>
#include <plugin-support.h>

namespace streamrove {

namespace {

constexpr const char *kFile = "settings.json";

std::string configPath(const char *file)
{
	char *path = obs_module_config_path(file);
	std::string out = path ? path : "";
	bfree(path);
	return out;
}

} // namespace

Settings loadSettings()
{
	Settings settings;
	const std::string path = configPath(kFile);
	if (path.empty()) {
		return settings;
	}
	obs_data_t *data = obs_data_create_from_json_file_safe(path.c_str(), "bak");
	if (!data) {
		return settings;
	}
	const char *baseUrl = obs_data_get_string(data, "baseUrl");
	const char *apiKey = obs_data_get_string(data, "apiKey");
	const char *streamId = obs_data_get_string(data, "streamId");
	if (baseUrl && *baseUrl) {
		settings.baseUrl = baseUrl;
	}
	if (apiKey) {
		settings.apiKey = apiKey;
	}
	if (streamId) {
		settings.streamId = streamId;
	}
	obs_data_release(data);
	return settings;
}

void saveSettings(const Settings &settings)
{
	const std::string dir = configPath("");
	if (dir.empty()) {
		return;
	}
	if (os_mkdirs(dir.c_str()) == MKDIR_ERROR) {
		obs_log(LOG_WARNING, "could not create config directory %s", dir.c_str());
		return;
	}
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "baseUrl", settings.baseUrl.c_str());
	obs_data_set_string(data, "apiKey", settings.apiKey.c_str());
	obs_data_set_string(data, "streamId", settings.streamId.c_str());
	const std::string path = configPath(kFile);
	if (!obs_data_save_json_safe(data, path.c_str(), "tmp", "bak")) {
		obs_log(LOG_WARNING, "could not save %s", path.c_str());
	}
	obs_data_release(data);
}

} // namespace streamrove
