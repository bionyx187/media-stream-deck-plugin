//==============================================================================
/**
@file       MediaStreamDeckPlugin.h

@brief      Shows current Windows media title on a button

@copyright  (c) 2021, bionyx187, based on code from Corsair.
			(c) 2018, Corsair Memory, Inc.
			This source code is licensed under the MIT-style license found in the LICENSE file.

**/
//==============================================================================

#include "MediaStreamDeckPlugin.h"
#include <atomic>

#include "Common/ESDConnectionManager.h"
#include "Common/EPLJSONUtils.h"

#include <winrt/Windows.Storage.Streams.h>

class CallBackTimer
{
public:
    CallBackTimer() :_execute(false) { }

    ~CallBackTimer()
    {
        if( _execute.load(std::memory_order_acquire) )
        {
            stop();
        };
    }

    void stop()
    {
        _execute.store(false, std::memory_order_release);
        if(_thd.joinable())
            _thd.join();
    }

    void start(int interval, std::function<int(int)> func)
    {
        if(_execute.load(std::memory_order_acquire))
        {
            stop();
        };
        _execute.store(true, std::memory_order_release);
        _thd = std::thread([this, interval, func]()
        {
            while (_execute.load(std::memory_order_acquire))
            {
                currentTick = func(currentTick);
                std::this_thread::sleep_for(std::chrono::milliseconds(interval));
            }
        });
    }

    bool is_running() const noexcept
    {
        return (_execute.load(std::memory_order_acquire) && _thd.joinable());
    }

	int currentTick;

private:
    std::atomic<bool> _execute;
    std::thread _thd;
};

MediaStreamDeckPlugin::MediaStreamDeckPlugin()
{
	// TODO: Configurable elements: mTextWidth should be configurable
	mTextWidth = 6;
	mMgr = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();

	mMgr.CurrentSessionChanged([this](GlobalSystemMediaTransportControlsSessionManager const& sender, CurrentSessionChangedEventArgs const& args) {
		if (this != nullptr && this->mConnectionManager != nullptr) {
			Log("Current Session Changed detected");
			LogSessions();
			auto currentSession = sender.GetCurrentSession();
			if (currentSession != nullptr) {
				currentSession.MediaPropertiesChanged({ this, &MediaStreamDeckPlugin::MediaChangedHandler });
				currentSession.PlaybackInfoChanged({ this, &MediaStreamDeckPlugin::PlaybackChangedHandler });
			}

		}
	});

	// Perform initial setup. If there's no active session now, our first hooks will be registered by the event handler
	// above.
	auto currentSession = mMgr.GetCurrentSession();
	if (currentSession != nullptr) {
		currentSession.MediaPropertiesChanged({ this, &MediaStreamDeckPlugin::MediaChangedHandler });
		currentSession.PlaybackInfoChanged({ this, &MediaStreamDeckPlugin::PlaybackChangedHandler });
	}
	// If there's no session, we can't log that fact since we don't have a logger yet.
}

MediaStreamDeckPlugin::~MediaStreamDeckPlugin()
{
	for (const auto& [key, value] : mContextTimers) {
		value->stop();
		delete value;
	}
}

// Convert a wide Unicode string to an UTF8 string
std::string MediaStreamDeckPlugin::UTF8Encode(const std::wstring& wstr)
{
	if (wstr.empty()) return std::string();
	int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
	std::string strTo(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
	return strTo;
}


void MediaStreamDeckPlugin::MediaChangedHandler(GlobalSystemMediaTransportControlsSession const& sender, MediaPropertiesChangedEventArgs const& args)
{
	// Since this are running in separate threads, it's possible the plugin could be destructed before they execute, so it must
	// verify 'this' is valid.
	if (this != nullptr) {
		Log("MediaPropertiesChanged detected");
		CheckMedia();
	}
}

void MediaStreamDeckPlugin::PlaybackChangedHandler(GlobalSystemMediaTransportControlsSession const& sender, PlaybackInfoChangedEventArgs const& args)
{
	// Since this are running in separate threads, it's possible the plugin could be destructed before they execute, so it must
	// verify 'this' is valid.
	if (this != nullptr) {
		Log("PlaybackInfoChanged detected");
		CheckMedia();
	}
}

int MediaStreamDeckPlugin::RefreshTimer(int tick, const std::string& context)
{
	//
	// This is running in an independent thread.
	//
	if(mConnectionManager != nullptr)
	{
		std::string text;

		// Make a local copy of the title
		mTitleMutex.lock();
		std::wstring wtitle(mTitle);
		mTitleMutex.unlock();

		// Only draw the title if set (i.e. media is actually playing)
		if (wtitle.length() > 0) {
			// Pad the string for scrolling.
			wtitle.insert(0, mTextWidth, ' ');
			wtitle.append(mTextWidth, ' ');


			if (tick > (wtitle.length() - mTextWidth)) {
				tick = 0;
			}

			auto substring = wtitle.substr(tick, mTextWidth);
			text = UTF8Encode(substring);
		}


		// Make sure this is still a valid context by looking for our entry in the timer map. If it's not there, the button got deactivated
		// and we shouldn't draw.
		mContextTimersMutex.lock();
		if (mContextTimers.find(context) != mContextTimers.end()) {
			mConnectionManager->SetTitle(text, context, kESDSDKTarget_HardwareAndSoftware);
		}
		mContextTimersMutex.unlock();


		if (text.size() > 0) {
			return ++tick;
		}
	}
	return 0;
}

void MediaStreamDeckPlugin::CheckMedia() {
	if (mConnectionManager != nullptr)
	{
		LogSessions();

		std::wstring currentTitle;
		auto currentSession = mMgr.GetCurrentSession();

		if (currentSession != nullptr) {
			auto properties = currentSession.TryGetMediaPropertiesAsync().get();

			if (properties != nullptr) {
				auto title = properties.Title();
				auto status = currentSession.GetPlaybackInfo().PlaybackStatus();
				auto thumbnail = properties.Thumbnail();

				if (thumbnail != nullptr) {

					auto t_async = thumbnail.OpenReadAsync();
					auto result = t_async.get();
					Log("ContentType: " + UTF8Encode(result.ContentType().c_str()));
				}
				else {
					Log("NO THUMBNAIL");
				}

				if (status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
					currentTitle = title;
				}
			}
		}

		// If the current session isn't playing, let's see if they have something playing somewhere else. This isn't perfect because
		// Chrome does peculiar things with its sessions, but it's something. If you're listening to multiple things simultaneously,
		// Windows is going to get confused too.
		if (currentTitle.empty()) {
			auto sessions = mMgr.GetSessions();

			for (const auto &session: sessions) {
				auto properties = session.TryGetMediaPropertiesAsync().get();
				if (properties != nullptr) {
					auto title = properties.Title();
					auto status = session.GetPlaybackInfo().PlaybackStatus();

					if (status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
						currentTitle = title;
						break;
					}
				}

			}
		}

		mTitleMutex.lock();
		mTitle = currentTitle;
		mTitleMutex.unlock();
	}
}

void MediaStreamDeckPlugin::LogSessions()
{
#if DEBUG
	auto sessions = mMgr.GetSessions();

	if (sessions.Size() == 0) {
		Log("No Sessions");
		return;
	}

	auto i = 0;
	for (const auto& session : sessions) {
		++i;
		auto tlProps = session.GetTimelineProperties();
		auto async = session.TryGetMediaPropertiesAsync();
		auto properties = async.get();
		auto thumbnail = properties.Thumbnail();
		auto title = properties.Title();
		auto status = session.GetPlaybackInfo().PlaybackStatus();

		Log("Session #" + std::to_string(i) + " (" + std::to_string(static_cast<int>(status)) + ") " + winrt::to_string(title));
	}
#endif
}

void MediaStreamDeckPlugin::Log(const std::string& message)
{
#if DEBUG
	if (mConnectionManager != nullptr) {
		mConnectionManager->LogMessage(message);
	}
#endif
}

void MediaStreamDeckPlugin::WillAppearForAction(const std::string& inAction, const std::string& inContext, const json &inPayload, const std::string& inDeviceID)
{
	Log("WillAppearForAction: " + inAction + " context: " + inContext + " payload: " + inPayload.dump());
	ReceiveSettings(inAction, inContext, inPayload, inDeviceID);
}

void MediaStreamDeckPlugin::WillDisappearForAction(const std::string& inAction, const std::string& inContext, const json &inPayload, const std::string& inDeviceID)
{
	Log("WillDisappearForAction: " + inAction + " payload: " + inPayload.dump());
	// Remove the context and its associated timer
	mContextTimersMutex.lock();

	if (mContextTimers.find(inContext) != mContextTimers.end()) {
		delete mContextTimers[inContext];
		mContextTimers.erase(inContext);
	}

	mContextTimersMutex.unlock();
}

void MediaStreamDeckPlugin::ReceiveSettings(const std::string& inAction, const std::string& inContext, const json& inPayload, const std::string& inDeviceID)
{
	Log("ReceiveSettings: " + inAction + " context: " + inContext + " payload: " + inPayload.dump());
	json settings;
	EPLJSONUtils::GetObjectByName(inPayload, "settings", settings);
	auto refresh_time = EPLJSONUtils::GetIntByName(settings, "refresh_time");

	if (refresh_time == 0) {
		refresh_time = 250;
	}

	// This resets the display timer for the settings for this view.
	StartRefreshTimer(refresh_time, inContext);
	CheckMedia();
}

void MediaStreamDeckPlugin::StartRefreshTimer(int period, const std::string& context)
{
	mContextTimersMutex.lock();

	auto existing = mContextTimers.find(context);

	CallBackTimer* timer = nullptr;
	if (existing != mContextTimers.end()) {
		timer = existing->second;
	}
	else {
		timer = new CallBackTimer();
	}
	mContextTimers[context] = timer;
	mContextTimersMutex.unlock();

	timer->start(period, [this, context, timer](int tick)
	{
		return this->RefreshTimer(tick, context);
	});

}

void MediaStreamDeckPlugin::KeyDownForAction(const std::string& inAction, const std::string& inContext, const json& inPayload, const std::string& inDeviceID)
{
	// Nothing to do
}

void MediaStreamDeckPlugin::KeyUpForAction(const std::string& inAction, const std::string& inContext, const json& inPayload, const std::string& inDeviceID)
{
	// Nothing to do
}

void MediaStreamDeckPlugin::DeviceDidConnect(const std::string& inDeviceID, const json& inDeviceInfo)
{
	// Nothing to do
}

void MediaStreamDeckPlugin::DeviceDidDisconnect(const std::string& inDeviceID)
{
	// Nothing to do
}

void MediaStreamDeckPlugin::SendToPlugin(const std::string& inAction, const std::string& inContext, const json& inPayload, const std::string& inDeviceID)
{
	// Nothing to do
}
