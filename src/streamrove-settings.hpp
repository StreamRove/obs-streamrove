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
 * What the dock remembers between OBS sessions.
 *
 * Stored as JSON in the plugin's own config directory (obs_module_config_path),
 * next to OBS's other per-user settings. The API key is stored as typed: OBS
 * keeps stream keys the same way, and a key the user can revoke from the
 * StreamRove dashboard is the right shape of secret for a desktop plugin.
 */
struct Settings {
	std::string baseUrl = "https://streamrove.com";
	std::string apiKey;
	std::string streamId;
};

Settings loadSettings();
void saveSettings(const Settings &settings);

} // namespace streamrove
