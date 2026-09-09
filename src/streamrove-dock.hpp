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

#include <QWidget>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "streamrove-api.hpp"
#include "streamrove-settings.hpp"

class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTimer;

struct obs_data;
typedef struct obs_data obs_data_t;

namespace streamrove {

struct Destination {
	std::string id;
	std::string platform;
	std::string name;
	std::string status;
};

/** The part of GET /streams/{id} the dock acts on. */
struct StreamDetail {
	std::string id;
	std::string title;
	std::string status;
	std::string rtmps;
	std::string rtmp;
	std::string streamKey;
	bool inputMissing = false;
	std::vector<Destination> destinations;

	/** RTMPS when the provider offers it, RTMP otherwise. */
	const std::string &ingestServer() const { return rtmps.empty() ? rtmp : rtmps; }
};

/**
 * The "StreamRove" dock.
 *
 * One stream goes from OBS to StreamRove; StreamRove fans it out to every
 * destination attached to that stream. So the dock does three things: sign in
 * with a developer key, pick the stream, and point OBS's stream output at that
 * stream's ingest. Everything else it shows (destinations, health, recommended
 * encoder settings) is read from the same API the dashboard uses.
 *
 * Network calls run on a worker thread and report back on the UI thread; the
 * widget itself never blocks.
 */
class Dock : public QWidget {
	Q_OBJECT

public:
	explicit Dock(QWidget *parent = nullptr);
	~Dock() override;

	/** OBS started or stopped streaming (frontend event; already on the UI thread). */
	void streamingStateChanged();

	/** OBS is exiting: stop polling and drop every reply still on its way. */
	void shutdown();

private:
	void buildUi();
	void updateButtons();
	void setStatus(const QString &text, bool isError = false);

	void connectClicked();
	void disconnectFromApi();

	/**
	 * Device linking: ask the server for a pair of codes, send the person to
	 * the site with one of them, and poll with the other until a key comes
	 * back. See DeviceAuth on the server for why it is shaped this way.
	 */
	void startLinking();
	void pollLinking();
	void cancelLinking();
	void finishLinking(const std::string &apiKey);
	void setLinkingUi(bool linking);
	void openVerificationPage();

	void loadStreams();
	void streamChosen(int index);
	void loadDetail();
	void poll();
	void applyToObs();
	void toggleLive();
	void destinationToggled(QListWidgetItem *item);
	void openInBrowser();

	void renderDetail();
	void renderHealth(obs_data_t *report);
	void renderRecommended(obs_data_t *profile);

	/** Run `work` off the UI thread, then `done` back on it (skipped if the dock is gone). */
	void runAsync(std::function<HttpResult()> work, std::function<void(const HttpResult &)> done);

	/**
	 * Shared with worker threads. A thread takes the mutex, checks `alive`,
	 * and queues its reply while still holding it, so the dock cannot be
	 * destroyed between the check and the post.
	 */
	struct Liveness {
		std::mutex mutex;
		bool alive = true;
	};

	Settings settings_;
	std::shared_ptr<ApiClient> api_;
	std::shared_ptr<Liveness> liveness_;
	bool connected_ = false;
	bool applied_ = false;
	StreamDetail current_;

	/** Set while a device link is in flight; empty otherwise. */
	std::string deviceCode_;
	std::string userCode_;
	std::string verificationUrl_;

	QLineEdit *serverEdit_ = nullptr;
	QLineEdit *keyEdit_ = nullptr;
	QWidget *keyRow_ = nullptr;
	QPushButton *connectBtn_ = nullptr;
	QPushButton *keyToggleBtn_ = nullptr;
	QPushButton *cancelLinkBtn_ = nullptr;
	QLabel *statusLabel_ = nullptr;
	QLabel *hintLabel_ = nullptr;
	QLabel *codeLabel_ = nullptr;

	QTimer *linkTimer_ = nullptr;

	QGroupBox *streamGroup_ = nullptr;
	QComboBox *streamCombo_ = nullptr;
	QPushButton *refreshBtn_ = nullptr;
	QPushButton *openBtn_ = nullptr;
	QLabel *streamStatus_ = nullptr;
	QLabel *destinationsTitle_ = nullptr;
	QListWidget *destinations_ = nullptr;
	QLabel *healthLabel_ = nullptr;
	QLabel *recommendedLabel_ = nullptr;
	QPushButton *applyBtn_ = nullptr;
	QPushButton *liveBtn_ = nullptr;

	QTimer *pollTimer_ = nullptr;
};

} // namespace streamrove
