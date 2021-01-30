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
	// TODO: Configurable elements: textwidth (based on title font, can we query that?), update time, polling time
	mTextWidth = 6;
	mTicks = 0;


	mMediaCheckTimer = new CallBackTimer();
	mMediaCheckTimer->start(1000, [this]() {
		this->CheckMedia();
	});

	mDisplayTimer = new CallBackTimer();
	mDisplayTimer->start(250, [this]()
	{
		this->UpdateTimer();
	});
}

MediaStreamDeckPlugin::~MediaStreamDeckPlugin()
{
	if(mDisplayTimer != nullptr)
	{
		mDisplayTimer->stop();
		
		delete mDisplayTimer;
		mDisplayTimer = nullptr;
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

void MediaStreamDeckPlugin::UpdateTimer()
{
	//
	// Warning: UpdateTimer() is running in the timer thread
	//
	if(mConnectionManager != nullptr)
	{
		mDataMutex.lock();

		if (mTitle.size() > 0) {
			if (mTicks > (mTitle.size())) {
				mTicks = 0;
			}
			std::wstring title{ mTitle };
			auto substring = title.substr(mTicks, mTextWidth);
			mTicks++;

			auto text = utf8_encode(substring);
			mConnectionManager->LogMessage("UpdateTimer mTicks:" + std::to_string(mTicks) + " mTitle: " + winrt::to_string(mTitle) + " substring: " + text);

			mVisibleContextsMutex.lock();
			for (const std::string& context : mVisibleContexts)
			{
				mConnectionManager->SetTitle(text, context, kESDSDKTarget_HardwareAndSoftware);
			}

		}
		mVisibleContextsMutex.unlock();
		mConnectionManager->LogMessage("UpdateTimer done");
		mDataMutex.unlock();
	}
}

void MediaStreamDeckPlugin::CheckMedia() {
	if (mConnectionManager != nullptr)
	{
		mConnectionManager->LogMessage("CheckMedia waiting for lock");

		mDataMutex.lock();

		auto sessions = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get().GetSessions();
		mConnectionManager->LogMessage("CheckMedia mTicks:" + std::to_string(mTicks) + " mTitle: " + winrt::to_string(mTitle));

		for (unsigned int i = 0; i < sessions.Size(); i++) {
			auto session = sessions.GetAt(i);
			auto tlProps = session.GetTimelineProperties();
			auto properties = session.TryGetMediaPropertiesAsync().get();
			auto status = session.GetPlaybackInfo().PlaybackStatus();
			// TODO: this padding should be dynamic based on mTextWidth
			auto title = properties.Title();


			if (status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
				if (title != mTitle) {
					mTicks = 0;
				}
				mTitle = title;
				break;
			}
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
	// Remember the context
	mVisibleContextsMutex.lock();
	mVisibleContexts.insert(inContext);
	mVisibleContextsMutex.unlock();
}

void MediaStreamDeckPlugin::WillDisappearForAction(const std::string& inAction, const std::string& inContext, const json &inPayload, const std::string& inDeviceID)
{
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

void MediaStreamDeckPlugin::SendToPlugin(const std::string& inAction, const std::string& inContext, const json &inPayload, const std::string& inDeviceID)
{
	// Nothing to do
}
