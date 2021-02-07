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
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Security.Cryptography.h>

using namespace Windows::Graphics::Imaging;
using namespace Windows::Storage::Streams;
using namespace Windows::Security::Cryptography;

class ButtonHandler
{
public:
    ButtonHandler() :_execute(false), textWidth(0) { }

    ~ButtonHandler()
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

	void set_refresh(bool refresh) {
		std::lock_guard<std::mutex> lock(mutex);
		doRefresh = refresh;
	}

	void set_text_width(int newWidth) {
		std::lock_guard<std::mutex> lock(mutex);
		textWidth = newWidth;
	}

	bool refresh() {
		std::lock_guard<std::mutex> lock(mutex);
		return doRefresh;
	}

	int text_width() {
		std::lock_guard<std::mutex> lock(mutex);
		return textWidth;
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
				if (currentTick > 0) {
					set_refresh(false);
				}

				if (refresh()) {
					// We haven't done the initial draw. We don't want to sleep long waiting to get unblocked,
					// so we sleep a fixed amount of time here to limit UI latency. If they have a slow scroll,
					// this avoids a slow initial draw.
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
				}
				else {
					std::this_thread::sleep_for(std::chrono::milliseconds(interval));
				}
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


	bool doRefresh;
	int textWidth;
	std::mutex mutex;
};

MediaStreamDeckPlugin::MediaStreamDeckPlugin()
{
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

	CheckMedia();
}

MediaStreamDeckPlugin::~MediaStreamDeckPlugin()
{
	for (const auto& [_, value] : mContextHandlers) {
		if (value != nullptr) {
			delete value;
		}
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

int MediaStreamDeckPlugin::RefreshTimer(int tick, const std::string& context, bool refresh, int textWidth)
{
	//
	// This is running in an independent thread. The object calling this is initialized in multiple steps.
	// The test below is verifying that all the invariants are established.
	//
	if(mConnectionManager != nullptr && textWidth != 0)
	{
		std::string text;
		std::wstring wtitle;

		// Read the global media data
		{
			std::lock_guard<std::mutex> lock(mButtonDataMutex);
			if (refresh) {
				Log("Context " + context + " doing refresh");
				mConnectionManager->SetImage(mImage, context, kESDSDKTarget_HardwareAndSoftware);
			}
			wtitle = mTitle;
		}

		// Only draw the title if set (i.e. media is actually playing)
		if (wtitle.length() > 0) {
			// Pad the string for scrolling.
			wtitle.insert(0, textWidth, ' ');
			wtitle.append(textWidth, ' ');


			if (tick > (wtitle.length() - textWidth)) {
				tick = 0;
			}

			auto substring = wtitle.substr(tick, textWidth);
			text = UTF8Encode(substring);
		}

		// Apply the scrolling version of the title text
		mConnectionManager->SetTitle(text, context, kESDSDKTarget_HardwareAndSoftware);

		// Only update our animation count if we actually drew something. This ensures
		// that we stay 
		if (text.size() > 0) {
			return ++tick;
		}
	}
	return 0;
}


// CheckMedia is called at initial plugin startup to sample the media state
// and then called in event handlers to sample media changes.
// Nothing in CheckMedia should depend on the plugin infra running. Calling Log and friends
// is OK, but this shouldn't assume the connection manager is up, since it's being called
// at object construction.
void MediaStreamDeckPlugin::CheckMedia() {
	LogSessions();

	std::wstring currentTitle;

	// Get the current session. There may not be one at startup or we just happen to catch them switching apps.
	auto currentSession = mMgr.GetCurrentSession();
	if (currentSession != nullptr) {
		auto properties = currentSession.TryGetMediaPropertiesAsync().get();

		if (properties != nullptr) {
			auto title = properties.Title();
			auto info = currentSession.GetPlaybackInfo();
			if (info != nullptr) {
				auto status = info.PlaybackStatus();

				if (status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
					currentTitle = title;
				}
			}
		}
	}

	// If the current session isn't playing (or doesn't exist), let's see if they have something playing somewhere else. 
	// This isn't perfect because Chrome will hide multiple playing videos behind a single session and only report
	// focused tabs, so we may not get anything if that playing tab isn't active.
	if (currentTitle.empty()) {
		auto sessions = mMgr.GetSessions();

		for (const auto &session: sessions) {
			auto properties = session.TryGetMediaPropertiesAsync().get();
			if (properties != nullptr) {
				auto title = properties.Title();
				auto info = session.GetPlaybackInfo();
				if (info != nullptr) {
					auto status = info.PlaybackStatus();

					if (status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
						currentTitle = title;
						currentSession = session;
						break;
					}
				}
			}
		}
	}

	std::string currentImage;

	if (!currentTitle.empty()) {
		// We need to try drawing whenever we have a title. I'm seeing two MediaPropertiesChangedEvents. The first one covers the title and what not,
		// the second one is the thumbnail. I don't want to have to rely on that always being the case, so I just fetch the thumbnail every time
		// and it all ends up eventually correct.
		auto properties = currentSession.TryGetMediaPropertiesAsync().get();

		if (properties != nullptr) {
			auto thumbnail = properties.Thumbnail();

			if (thumbnail != nullptr) {
				// The decoder is auto-configuring so it'll read the input data (which has always been PNG so far)
				// but it needs to be encoded as a base64-encoded string of PNG data.
				auto stream = thumbnail.OpenReadAsync().get();
				auto decoder = BitmapDecoder::CreateAsync(stream).get();

				// Scale the image down to 72x72 for the button by applying the transform here and requesting 72x72 on the encoder.
				BitmapTransform transform;
				transform.ScaledHeight(72);
				transform.ScaledWidth(72);
				auto pixels = decoder.GetPixelDataAsync(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Straight, transform, ExifOrientationMode::RespectExifOrientation, ColorManagementMode::ColorManageToSRgb).get();
				InMemoryRandomAccessStream outStream;
				auto encoder = BitmapEncoder::CreateAsync(BitmapEncoder::PngEncoderId(), outStream).get();
				auto dpiX = decoder.DpiX();
				auto dpiY = decoder.DpiY();
				auto pixelData = pixels.DetachPixelData();
				encoder.SetPixelData(decoder.BitmapPixelFormat(), BitmapAlphaMode::Ignore, 72, 72, dpiX, dpiY, pixelData);
				encoder.FlushAsync().get();

				// At this point outStream has the PNG-encoded data. We reset the stream for reading, create a buffer to hold the data
				// and read into the buffer.
				outStream.Seek(0);
				auto size = static_cast<uint32_t>(outStream.Size());
				auto buffer = Buffer(size);
				outStream.ReadAsync(buffer, size, InputStreamOptions::None).get();

				// Finally we generate the base64-encoded string, UTF8Encode that to convert from wstring to string and we're done!
				auto encoded = CryptographicBuffer::EncodeToBase64String(buffer);
				currentImage = UTF8Encode(encoded.c_str());
				Log("Fetched background image for " + UTF8Encode(currentTitle) + " size: " + std::to_string(outStream.Size()) + " encoded length: " + std::to_string(currentImage.size()));
			}
		}
	}

	// Update the variables to indicate the current title and thumbnail
	{
		std::lock_guard<std::mutex> lock(mButtonDataMutex);
		mImage = currentImage;
		mTitle = currentTitle;
	}

	// Tell all buttons we have new data and go get it!
	RefreshAllHandlers();
}

void MediaStreamDeckPlugin::RefreshAllHandlers()
{
	std::lock_guard<std::mutex> lock(mContextHandlersMutex);
	for (const auto& [context, handler] : mContextHandlers) {
		Log("Set broadcast refresh for context " + context);
		handler->set_refresh(true);
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
		auto properties = session.TryGetMediaPropertiesAsync().get();
		auto message = "Session #" + std::to_string(i) + " ";
		if (properties != nullptr) {
			auto title = properties.Title();
			message += winrt::to_string(title);
			auto info = session.GetPlaybackInfo();
			if (info != nullptr) {
				auto status = info.PlaybackStatus();
				message += " (" + std::to_string(static_cast<int>(status)) + ")";
			}
		}
		Log(message);
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
	// Since ReceiveSettings is called when a button is reconfigured, and receives the same payload, just delegate to that function
	// to configure the button.
	ReceiveSettings(inAction, inContext, inPayload, inDeviceID);
}

void MediaStreamDeckPlugin::WillDisappearForAction(const std::string& inAction, const std::string& inContext, const json &inPayload, const std::string& inDeviceID)
{
	Log("WillDisappearForAction: " + inAction + " payload: " + inPayload.dump());
	// Remove the context and its associated timer

	// Since the worker thread acquires the lock to figure out if it's getting removed, we acquire the lock
	// to remove its entry, then delete the object so it can run to completion and properly terminate.
	// Holding the lock for too long here results in deadlock.
	auto target = mContextHandlers.find(inContext);
	if (target != mContextHandlers.end()) {
		auto timer = target->second;
		{
			std::lock_guard<std::mutex> lock(mContextHandlersMutex);
			mContextHandlers.erase(inContext);
		}

		delete timer;
	}
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
	// We set an empty title now so we can get the response containing the font size so we configure font spacing.
	mConnectionManager->SetTitle("", inContext, kESDSDKTarget_HardwareAndSoftware);

	// This resets the display timer for the settings for this view.
	StartRefreshTimer(refresh_time, inContext);
}

void MediaStreamDeckPlugin::StartRefreshTimer(int period, const std::string& context)
{
	std::lock_guard<std::mutex> lock(mContextHandlersMutex);

	// Reuse existing handlers if possible
	auto handler = mContextHandlers[context];
	if (handler == nullptr) {
		handler = new ButtonHandler();
		mContextHandlers[context] = handler;
	}

	handler->set_refresh(true);
	handler->start(period, [this, context, handler](int tick)
	{
		return this->RefreshTimer(tick, context, handler->refresh(), handler->text_width());
	});
}

void MediaStreamDeckPlugin::TitleParametersDidChange(const std::string& inAction, const std::string& inContext, const json& inPayload, const std::string& inDeviceID)
{
	// We use this event to fish out the title text size and adjust the handler's text width based on it.
	Log("TitleParametersDidChange: " + inAction + " context: " + inContext + " payload: " + inPayload.dump());
	json params;
	EPLJSONUtils::GetObjectByName(inPayload, "titleParameters", params);
	auto font_size = EPLJSONUtils::GetIntByName(params, "fontSize");

	// Although this should exist, if the user went through profiles really quickly, we could get the deletion message
	// before the font response, so we don't want to crash in that case.
	std::lock_guard<std::mutex> lock(mContextHandlersMutex);
	auto existing = mContextHandlers.find(inContext);
	if (existing != mContextHandlers.end()) {
		existing->second->set_text_width(72 / (font_size / 2));
	}
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

