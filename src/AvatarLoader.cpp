#include "AvatarLoader.h"
#include "ApplicationGlobal.h"
#include "MainWindow.h"
#include "MemoryReader.h"
#include "UserEvent.h"
#include "webclient.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>

namespace {
const int MAX_CACHE_COUNT = 1000;
const int ICON_SIZE = 128;
}

using WebClientPtr = std::shared_ptr<WebClient>;

struct AvatarLoader::Private {
	volatile bool interrupted = false;
	std::mutex mutex;
	std::condition_variable condition;
	std::thread thread;
	std::deque<AvatarLoader::RequestItem> requests;
	MainWindow *mainwindow = nullptr;
	WebClientPtr web;

	std::set<QObject *> listeners;
};

AvatarLoader::AvatarLoader()
	: m(new Private)
{
}

AvatarLoader::~AvatarLoader()
{
	delete m;
}

bool AvatarLoader::isInterruptionRequested() const
{
	return m->interrupted;
}

void AvatarLoader::requestInterruption()
{
	m->interrupted = true;
	m->condition.notify_all();
}



void AvatarLoader::run()
{
	m->web = std::make_shared<WebClient>(&global->webcx);

	std::chrono::system_clock::time_point time_notify;

	const int NOTIFY_DELAY_ms = 500;

	bool notify = false;

	while (1) {
		RequestItem request;
		{
			std::unique_lock lock(m->mutex);
			if (isInterruptionRequested()) return;
			for (int i = 0; i < m->requests.size(); i++) {
				if (m->requests[i].state == Idle) {
					m->requests[i].state = Busy;
					request = m->requests[i];
					break;
				}
			}
			if (request.state == Idle) {
				std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
				if (now < time_notify) {
					m->condition.wait_for(lock, time_notify - now);
					notify = true;
				} else {
					if (notify) {
						notify = false;
						qDebug() << "notify";
						for (QObject *listener : m->listeners) {
							QApplication::postEvent(listener, new QEvent((QEvent::Type)UserEvent::AvatarReady));
						}
					}
					m->condition.wait(lock);
				}
				time_notify = std::chrono::system_clock::time_point();
			}
			if (isInterruptionRequested()) return;
		}

		if (request.state == Busy) {
			if (strchr(request.email.c_str(), '@')) {
				QString id;
				{
					QCryptographicHash hash(QCryptographicHash::Md5);
					hash.addData(request.email.c_str(), request.email.size());
					QByteArray ba = hash.result();
					char tmp[100];
					for (int i = 0; i < ba.size(); i++) {
						sprintf(tmp + i * 2, "%02x", ba.data()[i] & 0xff);
					}
					id = tmp;
				}
				QString url = "https://www.gravatar.com/avatar/%1?s=%2";
				url = url.arg(id).arg(ICON_SIZE);
				if (m->web->get(WebClient::Request(url.toStdString())) == 200) {
					if (!m->web->response().content.empty()) {
						MemoryReader reader(m->web->response().content.data(), m->web->response().content.size());
						reader.open(MemoryReader::ReadOnly);
						QImage image;
						image.load(&reader, nullptr);
						int w = image.width();
						int h = image.height();
						if (w > 0 && h > 0) {
							request.image = image;
							request.state = Done;
						}
					}
				} else {
					m->mainwindow->emitWriteLog(QString("Failed to fetch the avatar.\n").toUtf8());
					QString msg = QString::fromStdString(m->web->error().message() + '\n');
					m->mainwindow->emitWriteLog(msg.toUtf8());
				}
			}
			{
				std::lock_guard lock(m->mutex);
				for (int i = 0; i < m->requests.size(); i++) {
					if (m->requests[i].email == request.email) {
						if (request.state == Done) {
							m->requests[i] = request;
							time_notify = std::chrono::system_clock::now() + std::chrono::milliseconds(NOTIFY_DELAY_ms);
						} else {
							m->requests.erase(m->requests.begin() + i);
						}
						break;
					}
				}
			}
		}
	}
}

void AvatarLoader::start(MainWindow *mainwindow)
{
	m->mainwindow = mainwindow;
	m->interrupted = false;
	m->thread = std::thread([&](){
		run();
	});
}

void AvatarLoader::stop()
{
	requestInterruption();
	if (m->thread.joinable()) {
		m->thread.join();
	}
	m->requests.clear();
	if (m->web) {
		m->web->close();
		m->web.reset();
	}
}

QIcon AvatarLoader::fetch(std::string const &email, bool request) const
{
	std::lock_guard lock(m->mutex);
	bool found = false;
	for (size_t i = 0; i < m->requests.size(); i++) {
		if (m->requests[i].email == email) {
			found = true;
			if (m->requests[i].state == Done) {
				RequestItem item = m->requests[i];
				m->requests.erase(m->requests.begin() + i);
				m->requests.insert(m->requests.begin(), item);
				return QIcon(QPixmap::fromImage(item.image));
			}
		}
	}
	if (request && !found) {
		RequestItem item;
		item.state = Idle;
		item.email = email;
		m->requests.push_back(item);
		m->condition.notify_all();
	}
	return QIcon();
}

void AvatarLoader::addListener(QObject *listener)
{
	m->listeners.insert(m->listeners.end(), listener);
}

void AvatarLoader::removeListener(QObject *listener)
{
	auto it = m->listeners.find(listener);
	if (it != m->listeners.end()) {
		m->listeners.erase(it);
	}
}

