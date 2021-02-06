//==============================================================================
/**
@file       MediaStreamDeckPlugin.h

@brief      Shows current Windows media title on a button

@copyright  (c) 2021, bionyx187, based on code from Corsair.
			(c) 2018, Corsair Memory, Inc.
			This source code is licensed under the MIT-style license found in the LICENSE file.

**/
//==============================================================================

#include "Common/ESDBasePlugin.h"
#include <mutex>
#include <set>

#include <winrt/base.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Foundation.Collections.h>

using namespace winrt;
using namespace Windows::Media::Control;

class CallBackTimer;

class MediaStreamDeckPlugin : public ESDBasePlugin
{
public:
	
	MediaStreamDeckPlugin();
	virtual ~MediaStreamDeckPlugin();
	
	void KeyDownForAction(const std::string& inAction, const std::string& inContext, const json &inPayload, const std::string& inDeviceID) override;
	void KeyUpForAction(const std::string& inAction, const std::string& inContext, const json &inPayload, const std::string& inDeviceID) override;
	
	void WillAppearForAction(const std::string& inAction, const std::string& inContext, const json &inPayload, const std::string& inDeviceID) override;
	void WillDisappearForAction(const std::string& inAction, const std::string& inContext, const json &inPayload, const std::string& inDeviceID) override;
	
	void DeviceDidConnect(const std::string& inDeviceID, const json &inDeviceInfo) override;
	void DeviceDidDisconnect(const std::string& inDeviceID) override;
	
	void SendToPlugin(const std::string& inAction, const std::string& inContext, const json &inPayload, const std::string& inDeviceID) override;
	void ReceiveSettings(const std::string& inAction, const std::string& inContext, const json& inPayload, const std::string& inDeviceID) override;

private:
	void StartRefreshTimer(int period, const std::string& context);
	int RefreshTimer(int tick, const std::string& context);
	void CheckMedia();
	void LogSessions();
	void Log(const std::string& message);
	void MediaChangedHandler(GlobalSystemMediaTransportControlsSession const& sender, MediaPropertiesChangedEventArgs  const& args);
	void PlaybackChangedHandler(GlobalSystemMediaTransportControlsSession const& sender, PlaybackInfoChangedEventArgs const& args);

	std::string UTF8Encode(const std::wstring& wstr);

	std::map<std::string, CallBackTimer*> mContextTimers;
	std::mutex mContextTimersMutex;

	std::wstring mTitle;
	std::mutex mTitleMutex;

	int mTextWidth;
	IGlobalSystemMediaTransportControlsSessionManager mMgr{ nullptr };
};
