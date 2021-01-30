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

    void start(int interval, std::function<void(void)> func)
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
                func();
                std::this_thread::sleep_for(std::chrono::milliseconds(interval));
            }
        });
    }

    bool is_running() const noexcept
    {
        return (_execute.load(std::memory_order_acquire) && _thd.joinable());
    }

private:
    std::atomic<bool> _execute;
    std::thread _thd;
};

MediaStreamDeckPlugin::MediaStreamDeckPlugin()
{
	// TODO: Configurable elements: textwidth should be configurable
	mTextWidth = 6;
	mTicks = 0;
	mMediaCheckTimer = new CallBackTimer();
	mRefreshTimer = new CallBackTimer();
}

MediaStreamDeckPlugin::~MediaStreamDeckPlugin()
{
	if(mRefreshTimer != nullptr)
	{
		mRefreshTimer->stop();
		
		delete mRefreshTimer;
		mRefreshTimer = nullptr;
	}

	if (mMediaCheckTimer != nullptr) {
		mMediaCheckTimer->stop();

		delete mMediaCheckTimer;
		mMediaCheckTimer = nullptr;
	}
}

// Convert a wide Unicode string to an UTF8 string
std::string MediaStreamDeckPlugin::utf8_encode(const std::wstring& wstr)
{
	if (wstr.empty()) return std::string();
	int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
	std::string strTo(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
	return strTo;
}

void MediaStreamDeckPlugin::RefreshTimer()
{
	//
	// Warning: UpdateTimer() is running in the timer thread
	//
	if(mConnectionManager != nullptr)
	{
		mDataMutex.lock();
		std::string text;

		// Only draw the title if set (i.e. media is actually playing)
		if (mTitle.size() > 0) {
			// Pad the string for scrolling.
			std::wstring wtitle = mTitle.c_str();
			wtitle.insert(0, mTextWidth, ' ');
			wtitle.append(mTextWidth, ' ');


			if (mTicks > (wtitle.length() - mTextWidth)) {
				mTicks = 0;
			}

			auto substring = wtitle.substr(mTicks, mTextWidth);
			text = utf8_encode(substring);
			//mConnectionManager->LogMessage("UpdateTimer mTicks:" + std::to_string(mTicks) + " mTitle: " + winrt::to_string(mTitle) + " substring: |" + text +"|");
			mTicks++;
		}

		mVisibleContextsMutex.lock();
		for (const std::string& context : mVisibleContexts)
		{
			mConnectionManager->SetTitle(text, context, kESDSDKTarget_HardwareAndSoftware);
		}
		mVisibleContextsMutex.unlock();
		mDataMutex.unlock();
	}
}

void MediaStreamDeckPlugin::CheckMedia() {
	if (mConnectionManager != nullptr)
	{
		//mConnectionManager->LogMessage("CheckMedia waiting for lock");

		mDataMutex.lock();

		auto sessions = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get().GetSessions();
		//mConnectionManager->LogMessage("CheckMedia mTicks:" + std::to_string(mTicks) + " mTitle: " + winrt::to_string(mTitle));

		bool isPlaying = false;
		for (unsigned int i = 0; i < sessions.Size(); i++) {
			auto session = sessions.GetAt(i);
			auto tlProps = session.GetTimelineProperties();
			auto properties = session.TryGetMediaPropertiesAsync().get();
			auto status = session.GetPlaybackInfo().PlaybackStatus();
			auto thumbnail = properties.Thumbnail();
			auto title = properties.Title();

			//mConnectionManager->LogMessage("Session #" + std::to_string(i) + " (" + std::to_string(static_cast<int>(status)) + ") " + winrt::to_string(title));


			if (status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
				if (title != mTitle) {
					mTicks = 0;
					mTitle = title;
				}
				isPlaying = true;
				break;
			}
		}

		if (!isPlaying) {
			mTitle = winrt::to_hstring("");
			mTicks = 0;
		}
		mDataMutex.unlock();
	}
}

void MediaStreamDeckPlugin::KeyDownForAction(const std::string& inAction, const std::string& inContext, const json &inPayload, const std::string& inDeviceID)
{
	// Nothing to do
}

void MediaStreamDeckPlugin::KeyUpForAction(const std::string& inAction, const std::string& inContext, const json &inPayload, const std::string& inDeviceID)
{
	// Nothing to do
}

void MediaStreamDeckPlugin::WillAppearForAction(const std::string& inAction, const std::string& inContext, const json &inPayload, const std::string& inDeviceID)
{
	//mConnectionManager->LogMessage("WillAppearForAction");
	// Remember the context
	mVisibleContextsMutex.lock();
	mVisibleContexts.insert(inContext);
	mVisibleContextsMutex.unlock();
	ReceiveSettings(inAction, inContext, inPayload, inDeviceID);
}

void MediaStreamDeckPlugin::WillDisappearForAction(const std::string& inAction, const std::string& inContext, const json &inPayload, const std::string& inDeviceID)
{
	//mConnectionManager->LogMessage("WillDisappearForAction");

	// Remove the context
	mVisibleContextsMutex.lock();
	mVisibleContexts.erase(inContext);
	mVisibleContextsMutex.unlock();
}

void MediaStreamDeckPlugin::DeviceDidConnect(const std::string& inDeviceID, const json &inDeviceInfo)
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

void MediaStreamDeckPlugin::ReceiveSettings(const std::string& inAction, const std::string& inContext, const json& inPayload, const std::string& inDeviceID)
{
	json settings;
	EPLJSONUtils::GetObjectByName(inPayload, "settings", settings);
	auto check_time = EPLJSONUtils::GetIntByName(settings, "check_time");
	auto refresh_time = EPLJSONUtils::GetIntByName(settings, "refresh_time");

	// TODO: save the settings and only restart the timers if they've changed since this op isn't so cheap...
	StartCheckTimer(check_time);
	StartRefreshTimer(refresh_time);
}

void MediaStreamDeckPlugin::StartCheckTimer(int period)
{
	mMediaCheckTimer->start(period, [this]() {
		this->CheckMedia();
	});
}

void MediaStreamDeckPlugin::StartRefreshTimer(int period)
{
	mRefreshTimer->start(period, [this]()
	{
		this->RefreshTimer();
	});
}
