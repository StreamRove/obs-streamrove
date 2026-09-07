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

#include "streamrove-dock.hpp"

#include <thread>

#include <QComboBox>
#include <QDesktopServices>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <obs.h>
#include <obs-data.h>
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

namespace streamrove {

namespace {

constexpr int kPollIntervalMs = 10000;

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

std::string str(obs_data_t *data, const char *key)
{
	const char *value = obs_data_get_string(data, key);
	return value ? value : "";
}

/** Parse a JSON body; nullptr when it is not JSON. Caller releases. */
obs_data_t *parse(const HttpResult &result)
{
	return result.body.empty() ? nullptr : obs_data_create_from_json(result.body.c_str());
}

/**
 * Point OBS's stream output at an RTMP(S) ingest.
 *
 * The same thing Settings → Stream does when a person picks "Custom": a fresh
 * rtmp_custom service replaces the current one and is saved with the profile.
 * The current service's hotkeys are carried over so nothing rebinds.
 */
bool setObsService(const std::string &server, const std::string &key)
{
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "server", server.c_str());
	obs_data_set_string(settings, "key", key.c_str());
	obs_data_set_bool(settings, "use_auth", false);
	obs_data_set_bool(settings, "bwtest", false);

	obs_service_t *current = obs_frontend_get_streaming_service();
	obs_data_t *hotkeys = current ? obs_hotkeys_save_service(current) : nullptr;

	obs_service_t *service = obs_service_create("rtmp_custom", "default_service", settings, hotkeys);
	obs_data_release(settings);
	if (hotkeys) {
		obs_data_release(hotkeys);
	}
	if (!service) {
		return false;
	}
	obs_frontend_set_streaming_service(service);
	obs_frontend_save_streaming_service();
	obs_service_release(service);
	return true;
}

} // namespace

Dock::Dock(QWidget *parent) : QWidget(parent), settings_(loadSettings()), liveness_(std::make_shared<Liveness>())
{
	buildUi();

	pollTimer_ = new QTimer(this);
	pollTimer_->setInterval(kPollIntervalMs);
	QObject::connect(pollTimer_, &QTimer::timeout, this, &Dock::poll);

	updateButtons();
	if (!settings_.apiKey.empty()) {
		connectClicked();
	}
}

Dock::~Dock()
{
	shutdown();
}

void Dock::shutdown()
{
	pollTimer_->stop();
	api_.reset();
	const std::lock_guard<std::mutex> lock(liveness_->mutex);
	liveness_->alive = false;
}

void Dock::buildUi()
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(8);

	// --- Account ---------------------------------------------------------
	auto *account = new QGroupBox(text("StreamRove.Account"), this);
	auto *form = new QFormLayout(account);
	form->setContentsMargins(8, 8, 8, 8);

	serverEdit_ = new QLineEdit(QString::fromStdString(settings_.baseUrl), account);
	serverEdit_->setPlaceholderText("https://streamrove.com");
	form->addRow(text("StreamRove.ServerUrl"), serverEdit_);

	keyEdit_ = new QLineEdit(QString::fromStdString(settings_.apiKey), account);
	keyEdit_->setEchoMode(QLineEdit::Password);
	keyEdit_->setPlaceholderText("mux_…");
	form->addRow(text("StreamRove.ApiKey"), keyEdit_);

	hintLabel_ = new QLabel(account);
	hintLabel_->setWordWrap(true);
	hintLabel_->setOpenExternalLinks(true);
	hintLabel_->setTextFormat(Qt::RichText);
	form->addRow(hintLabel_);

	auto *connectRow = new QHBoxLayout();
	connectBtn_ = new QPushButton(text("StreamRove.Connect"), account);
	connectBtn_->setDefault(true);
	statusLabel_ = new QLabel(text("StreamRove.Status.Disconnected"), account);
	statusLabel_->setWordWrap(true);
	connectRow->addWidget(connectBtn_);
	connectRow->addWidget(statusLabel_, 1);
	form->addRow(connectRow);

	QObject::connect(connectBtn_, &QPushButton::clicked, this, &Dock::connectClicked);
	QObject::connect(keyEdit_, &QLineEdit::returnPressed, this, &Dock::connectClicked);
	root->addWidget(account);

	// --- Stream ----------------------------------------------------------
	streamGroup_ = new QGroupBox(text("StreamRove.Streams"), this);
	auto *streamLayout = new QVBoxLayout(streamGroup_);
	streamLayout->setContentsMargins(8, 8, 8, 8);

	auto *pickRow = new QHBoxLayout();
	streamCombo_ = new QComboBox(streamGroup_);
	streamCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	refreshBtn_ = new QPushButton(text("StreamRove.Refresh"), streamGroup_);
	openBtn_ = new QPushButton(text("StreamRove.OpenInBrowser"), streamGroup_);
	pickRow->addWidget(streamCombo_, 1);
	pickRow->addWidget(refreshBtn_);
	pickRow->addWidget(openBtn_);
	streamLayout->addLayout(pickRow);

	streamStatus_ = new QLabel(streamGroup_);
	streamStatus_->setWordWrap(true);
	streamLayout->addWidget(streamStatus_);

	destinationsTitle_ = new QLabel(streamGroup_);
	streamLayout->addWidget(destinationsTitle_);
	destinations_ = new QListWidget(streamGroup_);
	destinations_->setSelectionMode(QAbstractItemView::NoSelection);
	destinations_->setMaximumHeight(140);
	streamLayout->addWidget(destinations_);

	healthLabel_ = new QLabel(streamGroup_);
	healthLabel_->setWordWrap(true);
	streamLayout->addWidget(healthLabel_);

	recommendedLabel_ = new QLabel(streamGroup_);
	recommendedLabel_->setWordWrap(true);
	recommendedLabel_->setStyleSheet("color: palette(mid);");
	streamLayout->addWidget(recommendedLabel_);

	auto *actions = new QHBoxLayout();
	applyBtn_ = new QPushButton(text("StreamRove.UseInObs"), streamGroup_);
	liveBtn_ = new QPushButton(text("StreamRove.GoLive"), streamGroup_);
	actions->addWidget(applyBtn_, 1);
	actions->addWidget(liveBtn_, 1);
	streamLayout->addLayout(actions);

	QObject::connect(streamCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &Dock::streamChosen);
	QObject::connect(refreshBtn_, &QPushButton::clicked, this, &Dock::loadStreams);
	QObject::connect(openBtn_, &QPushButton::clicked, this, &Dock::openInBrowser);
	QObject::connect(applyBtn_, &QPushButton::clicked, this, &Dock::applyToObs);
	QObject::connect(liveBtn_, &QPushButton::clicked, this, &Dock::toggleLive);
	QObject::connect(destinations_, &QListWidget::itemChanged, this, &Dock::destinationToggled);

	root->addWidget(streamGroup_);
	root->addStretch(1);
}

void Dock::updateButtons()
{
	const bool haveStream = connected_ && !current_.id.empty();
	const bool streaming = obs_frontend_streaming_active();

	serverEdit_->setEnabled(!connected_);
	keyEdit_->setEnabled(!connected_);
	connectBtn_->setText(text(connected_ ? "StreamRove.Disconnect" : "StreamRove.Connect"));

	const QString site = QString::fromStdString(ApiClient::normalizeBaseUrl(serverEdit_->text().toStdString()));
	hintLabel_->setText(text("StreamRove.ApiKey.Hint").arg(site + "/home"));

	streamGroup_->setEnabled(connected_);
	refreshBtn_->setEnabled(connected_);
	openBtn_->setEnabled(haveStream);
	applyBtn_->setEnabled(haveStream && !streaming);
	liveBtn_->setEnabled(haveStream || streaming);
	liveBtn_->setText(text(streaming ? "StreamRove.StopStreaming" : "StreamRove.GoLive"));
}

void Dock::setStatus(const QString &message, bool isError)
{
	statusLabel_->setText(message);
	statusLabel_->setStyleSheet(isError ? "color: #e0574f;" : "");
	if (isError) {
		obs_log(LOG_WARNING, "%s", message.toUtf8().constData());
	}
}

void Dock::runAsync(std::function<HttpResult()> work, std::function<void(const HttpResult &)> done)
{
	auto liveness = liveness_;
	std::thread([this, liveness, work = std::move(work), done = std::move(done)]() {
		const HttpResult result = work();
		// A call queued for a receiver that is destroyed afterwards is
		// dropped by Qt, so the lock only has to cover the check and the post.
		const std::lock_guard<std::mutex> lock(liveness->mutex);
		if (!liveness->alive) {
			return;
		}
		QMetaObject::invokeMethod(this, [done, result]() { done(result); }, Qt::QueuedConnection);
	}).detach();
}

// --- account -----------------------------------------------------------------

void Dock::connectClicked()
{
	if (connected_) {
		disconnectFromApi();
		return;
	}

	settings_.baseUrl = ApiClient::normalizeBaseUrl(serverEdit_->text().toStdString());
	settings_.apiKey = keyEdit_->text().trimmed().toStdString();
	serverEdit_->setText(QString::fromStdString(settings_.baseUrl));
	if (settings_.apiKey.empty()) {
		setStatus(text("StreamRove.Status.KeyMissing"), true);
		return;
	}
	saveSettings(settings_);

	api_ = std::make_shared<ApiClient>(settings_.baseUrl, settings_.apiKey);
	connectBtn_->setEnabled(false);
	setStatus(text("StreamRove.Status.Connecting"));

	auto api = api_;
	runAsync([api]() { return api->get("/auth/me"); },
		 [this](const HttpResult &result) {
			 connectBtn_->setEnabled(true);
			 if (!result.ok()) {
				 api_.reset();
				 setStatus(
					 text("StreamRove.Status.Error").arg(QString::fromStdString(result.describe())),
					 true);
				 updateButtons();
				 return;
			 }
			 QString who;
			 if (obs_data_t *data = parse(result)) {
				 if (obs_data_t *user = obs_data_get_obj(data, "user")) {
					 who = QString::fromStdString(str(user, "name"));
					 if (who.isEmpty()) {
						 who = QString::fromStdString(str(user, "email"));
					 }
					 obs_data_release(user);
				 }
				 obs_data_release(data);
			 }
			 connected_ = true;
			 setStatus(text("StreamRove.Status.Connected").arg(who.isEmpty() ? "StreamRove" : who));
			 updateButtons();
			 loadStreams();
			 pollTimer_->start();
		 });
}

void Dock::disconnectFromApi()
{
	pollTimer_->stop();
	connected_ = false;
	applied_ = false;
	api_.reset();
	current_ = StreamDetail{};
	{
		const QSignalBlocker block(streamCombo_);
		streamCombo_->clear();
	}
	destinations_->clear();
	streamStatus_->clear();
	destinationsTitle_->clear();
	healthLabel_->clear();
	recommendedLabel_->clear();
	setStatus(text("StreamRove.Status.Disconnected"));
	updateButtons();
}

// --- streams -----------------------------------------------------------------

void Dock::loadStreams()
{
	if (!api_) {
		return;
	}
	auto api = api_;
	runAsync([api]() { return api->get("/streams"); },
		 [this](const HttpResult &result) {
			 if (!result.ok()) {
				 setStatus(
					 text("StreamRove.Status.Error").arg(QString::fromStdString(result.describe())),
					 true);
				 return;
			 }
			 obs_data_t *data = parse(result);
			 if (!data) {
				 return;
			 }
			 const QSignalBlocker block(streamCombo_);
			 streamCombo_->clear();
			 int selected = -1;
			 obs_data_array_t *streams = obs_data_get_array(data, "streams");
			 const size_t count = streams ? obs_data_array_count(streams) : 0;
			 for (size_t i = 0; i < count; i++) {
				 obs_data_t *item = obs_data_array_item(streams, i);
				 const std::string id = str(item, "id");
				 const std::string title = str(item, "title");
				 const std::string status = str(item, "status");
				 QString label = QString::fromStdString(title.empty() ? id : title);
				 if (status == "Live") {
					 label += "  •  " + text("StreamRove.Stream.Live");
				 }
				 streamCombo_->addItem(label, QString::fromStdString(id));
				 if (id == settings_.streamId) {
					 selected = static_cast<int>(i);
				 }
				 obs_data_release(item);
			 }
			 if (streams) {
				 obs_data_array_release(streams);
			 }
			 obs_data_release(data);

			 if (count == 0) {
				 current_ = StreamDetail{};
				 streamStatus_->setText(text("StreamRove.NoStreams"));
				 updateButtons();
				 return;
			 }
			 streamCombo_->setCurrentIndex(selected >= 0 ? selected : 0);
			 streamChosen(streamCombo_->currentIndex());
		 });
}

void Dock::streamChosen(int index)
{
	if (index < 0) {
		return;
	}
	const std::string id = streamCombo_->itemData(index).toString().toStdString();
	if (id.empty()) {
		return;
	}
	if (id != current_.id) {
		applied_ = false;
	}
	settings_.streamId = id;
	saveSettings(settings_);
	current_ = StreamDetail{};
	current_.id = id;
	loadDetail();
}

void Dock::loadDetail()
{
	if (!api_ || current_.id.empty()) {
		return;
	}
	auto api = api_;
	const std::string id = current_.id;

	runAsync([api, id]() { return api->get("/streams/" + id); },
		 [this, id](const HttpResult &result) {
			 if (id != current_.id) {
				 return; // the user has moved on
			 }
			 if (!result.ok()) {
				 setStatus(
					 text("StreamRove.Status.Error").arg(QString::fromStdString(result.describe())),
					 true);
				 return;
			 }
			 obs_data_t *data = parse(result);
			 if (!data) {
				 return;
			 }
			 obs_data_t *stream = obs_data_get_obj(data, "stream");
			 if (stream) {
				 StreamDetail detail;
				 detail.id = str(stream, "id");
				 detail.title = str(stream, "title");
				 detail.status = str(stream, "status");
				 detail.streamKey = str(stream, "streamKey");
				 detail.inputMissing = obs_data_get_bool(stream, "providerInputMissing");
				 if (obs_data_t *ingest = obs_data_get_obj(stream, "ingest")) {
					 detail.rtmps = str(ingest, "rtmps");
					 detail.rtmp = str(ingest, "rtmp");
					 obs_data_release(ingest);
				 }
				 if (obs_data_array_t *dests = obs_data_get_array(stream, "destinations")) {
					 const size_t n = obs_data_array_count(dests);
					 for (size_t i = 0; i < n; i++) {
						 obs_data_t *d = obs_data_array_item(dests, i);
						 detail.destinations.push_back({str(d, "id"), str(d, "platform"),
										str(d, "displayName"),
										str(d, "status")});
						 obs_data_release(d);
					 }
					 obs_data_array_release(dests);
				 }
				 obs_data_release(stream);
				 current_ = detail;
				 renderDetail();
			 }
			 obs_data_release(data);
		 });

	runAsync([api, id]() { return api->get("/streams/" + id + "/output-profile"); },
		 [this, id](const HttpResult &result) {
			 if (id != current_.id || !result.ok()) {
				 return;
			 }
			 if (obs_data_t *data = parse(result)) {
				 if (obs_data_t *profile = obs_data_get_obj(data, "profile")) {
					 renderRecommended(profile);
					 obs_data_release(profile);
				 }
				 obs_data_release(data);
			 }
		 });

	runAsync([api, id]() { return api->get("/streams/" + id + "/health"); },
		 [this, id](const HttpResult &result) {
			 if (id != current_.id || !result.ok()) {
				 return;
			 }
			 if (obs_data_t *report = parse(result)) {
				 renderHealth(report);
				 obs_data_release(report);
			 }
		 });
}

void Dock::poll()
{
	if (!connected_ || !api_ || current_.id.empty()) {
		return;
	}
	auto api = api_;
	const std::string id = current_.id;

	runAsync([api, id]() { return api->get("/streams/" + id + "/live-status"); },
		 [this, id, api](const HttpResult &result) {
			 if (id != current_.id || !result.ok()) {
				 return;
			 }
			 if (obs_data_t *data = parse(result)) {
				 const std::string status = str(data, "status");
				 obs_data_release(data);
				 if (!status.empty() && status != current_.status) {
					 current_.status = status;
					 renderDetail();
				 }
			 }
			 if (current_.status != "Live") {
				 return;
			 }
			 runAsync([api, id]() { return api->get("/streams/" + id + "/health"); },
				  [this, id](const HttpResult &health) {
					  if (id != current_.id || !health.ok()) {
						  return;
					  }
					  if (obs_data_t *report = parse(health)) {
						  renderHealth(report);
						  obs_data_release(report);
					  }
				  });
		 });
}

// --- rendering ---------------------------------------------------------------

void Dock::renderDetail()
{
	const bool live = current_.status == "Live";
	QString status = live ? QStringLiteral("<b style=\"color:#e0574f\">%1</b>").arg(text("StreamRove.Stream.Live"))
			      : QStringLiteral("<b>%1</b>").arg(text("StreamRove.Stream.Ready"));
	if (current_.inputMissing) {
		status += "<br>" + text("StreamRove.UseInObs.NoKey");
	} else if (applied_) {
		status += "<br>" +
			  text("StreamRove.UseInObs.Done").arg(QString::fromStdString(current_.title).toHtmlEscaped());
	}
	streamStatus_->setTextFormat(Qt::RichText);
	streamStatus_->setText(status);

	destinationsTitle_->setText(text("StreamRove.Destinations").arg(current_.destinations.size()));
	{
		const QSignalBlocker block(destinations_);
		destinations_->clear();
		if (current_.destinations.empty()) {
			auto *item = new QListWidgetItem(text("StreamRove.Destinations.None"), destinations_);
			item->setFlags(Qt::NoItemFlags);
		}
		for (const Destination &d : current_.destinations) {
			QString label = QString::fromStdString(d.platform);
			if (!d.name.empty()) {
				label += " · " + QString::fromStdString(d.name);
			}
			if (d.status != "active") {
				label += "  (" + QString::fromStdString(d.status) + ")";
			}
			auto *item = new QListWidgetItem(label, destinations_);
			item->setData(Qt::UserRole, QString::fromStdString(d.id));
			item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
			item->setCheckState(d.status == "active" ? Qt::Checked : Qt::Unchecked);
		}
	}
	updateButtons();
}

void Dock::renderHealth(obs_data_t *report)
{
	const std::string status = str(report, "status");
	QString line = text("StreamRove.Health") + ": ";
	if (status.empty()) {
		line += text("StreamRove.Health.NoData");
	} else {
		line += QString::fromStdString(status).toUpper();
	}
	if (obs_data_t *score = obs_data_get_obj(report, "score")) {
		if (obs_data_has_user_value(score, "value")) {
			line += QStringLiteral(" · %1/100").arg(obs_data_get_int(score, "value"));
		}
		obs_data_release(score);
	}
	if (status == "warning" || status == "critical") {
		if (obs_data_array_t *events = obs_data_get_array(report, "events")) {
			if (obs_data_array_count(events) > 0) {
				obs_data_t *latest = obs_data_array_item(events, 0);
				const std::string message = str(latest, "message");
				if (!message.empty()) {
					line += "<br>" + QString::fromStdString(message).toHtmlEscaped();
				}
				obs_data_release(latest);
			}
			obs_data_array_release(events);
		}
	}
	healthLabel_->setTextFormat(Qt::RichText);
	healthLabel_->setText(line);
}

void Dock::renderRecommended(obs_data_t *profile)
{
	obs_data_t *settings = obs_data_get_obj(profile, "settings");
	if (!settings) {
		recommendedLabel_->clear();
		return;
	}
	const long long width = obs_data_get_int(settings, "width");
	const long long height = obs_data_get_int(settings, "height");
	const long long fps = obs_data_get_int(settings, "fps");
	const long long video = obs_data_get_int(settings, "videoBitrateKbps");
	const long long maxVideo = obs_data_get_int(settings, "maxVideoBitrateKbps");
	const long long keyint = obs_data_get_int(settings, "keyframeIntervalS");
	const long long audio = obs_data_get_int(settings, "audioBitrateKbps");
	obs_data_release(settings);

	QStringList parts;
	if (width && height) {
		parts << QStringLiteral("%1×%2").arg(width).arg(height) +
				 (fps ? QStringLiteral(" @ %1 fps").arg(fps) : "");
	}
	if (video) {
		parts << (maxVideo && maxVideo != video ? QStringLiteral("%1 kbps (max %2)").arg(video).arg(maxVideo)
							: QStringLiteral("%1 kbps").arg(video));
	}
	if (keyint) {
		parts << text("StreamRove.Recommended.Keyframe").arg(keyint);
	}
	if (audio) {
		parts << text("StreamRove.Recommended.Audio").arg(audio);
	}
	recommendedLabel_->setText(parts.isEmpty() ? QString() : text("StreamRove.Recommended").arg(parts.join(" · ")));
}

// --- actions -----------------------------------------------------------------

void Dock::applyToObs()
{
	if (current_.id.empty()) {
		return;
	}
	if (obs_frontend_streaming_active()) {
		QMessageBox::information(this, text("StreamRove.Dock.Title"), text("StreamRove.Confirm.StopFirst"));
		return;
	}
	const std::string server = current_.ingestServer();
	if (server.empty() || current_.streamKey.empty() || current_.inputMissing) {
		setStatus(text("StreamRove.UseInObs.NoKey"), true);
		return;
	}
	if (!setObsService(server, current_.streamKey)) {
		setStatus(text("StreamRove.UseInObs.Failed"), true);
		return;
	}
	applied_ = true;
	obs_log(LOG_INFO, "OBS stream output set to StreamRove stream %s", current_.id.c_str());
	renderDetail();
}

void Dock::toggleLive()
{
	if (obs_frontend_streaming_active()) {
		obs_frontend_streaming_stop();
		return;
	}
	if (!applied_) {
		applyToObs();
	}
	if (applied_) {
		obs_frontend_streaming_start();
	}
}

void Dock::streamingStateChanged()
{
	updateButtons();
	// StreamRove notices the encoder a few seconds after OBS connects; ask then
	// rather than waiting for the next scheduled poll.
	QTimer::singleShot(4000, this, &Dock::poll);
}

void Dock::destinationToggled(QListWidgetItem *item)
{
	if (!api_ || !item || current_.id.empty()) {
		return;
	}
	const std::string destId = item->data(Qt::UserRole).toString().toStdString();
	if (destId.empty()) {
		return;
	}
	const bool enabled = item->checkState() == Qt::Checked;
	const std::string body = enabled ? "{\"enabled\":true}" : "{\"enabled\":false}";
	auto api = api_;
	const std::string streamId = current_.id;
	destinations_->setEnabled(false);
	runAsync([api, streamId, destId,
		  body]() { return api->patch("/streams/" + streamId + "/destinations/" + destId, body); },
		 [this, streamId](const HttpResult &result) {
			 destinations_->setEnabled(true);
			 if (!result.ok()) {
				 setStatus(
					 text("StreamRove.Status.Error").arg(QString::fromStdString(result.describe())),
					 true);
			 }
			 if (streamId == current_.id) {
				 loadDetail(); // re-read: the server decides what "enabled" means
			 }
		 });
}

void Dock::openInBrowser()
{
	if (current_.id.empty()) {
		return;
	}
	const std::string url = ApiClient::normalizeBaseUrl(settings_.baseUrl) + "/streams/" + current_.id;
	QDesktopServices::openUrl(QUrl(QString::fromStdString(url)));
}

} // namespace streamrove
