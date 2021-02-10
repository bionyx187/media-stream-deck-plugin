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
#include <map>

#include <winrt/base.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Foundation.Collections.h>

class ButtonHandler;

class MediaStreamDeckPlugin : public ESDBasePlugin
{
public:
	MediaStreamDeckPlugin();
	virtual ~MediaStreamDeckPlugin();

	void WillAppearForAction(const std::string& inAction, const std::string& inContext, const json &inPayload, const std::string& inDeviceID);
	void WillDisappearForAction(const std::string& inAction, const std::string& inContext, const json &inPayload, const std::string& inDeviceID);
	void ReceiveSettings(const std::string& inAction, const std::string& inContext, const json& inPayload, const std::string& inDeviceID);
	void TitleParametersDidChange(const std::string& inAction, const std::string& inContext, const json& inPayload, const std::string& inDeviceID);

	void DeviceDidConnect(const std::string& inDeviceID, const json& inDeviceInfo) {};
	void DeviceDidDisconnect(const std::string& inDeviceID) {};
	void KeyDownForAction(const std::string& inAction, const std::string& inContext, const json& inPayload, const std::string& inDeviceID) {};
	void KeyUpForAction(const std::string& inAction, const std::string& inContext, const json& inPayload, const std::string& inDeviceID) {};
	void SendToPlugin(const std::string& inAction, const std::string& inContext, const json& inPayload, const std::string& inDeviceID) {};

private:
	void StartButtonHandler(int period, const std::string& context);
	int HandleButton(int tick, const std::string& context, bool refresh, int textWidth);
	void CheckMedia();

	void RefreshAllHandlers();

	void LogSessions();
	void Log(const std::string& message);

	void MediaChangedHandler(winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession const& sender, winrt::Windows::Media::Control::MediaPropertiesChangedEventArgs  const& args);
	void PlaybackChangedHandler(winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession const& sender, winrt::Windows::Media::Control::PlaybackInfoChangedEventArgs const& args);

	std::string UTF8Encode(const std::wstring& wstr);

	std::map<std::string, ButtonHandler*> mContextHandlers;
	std::mutex mContextHandlersMutex; // protects mContextHandlers

	std::wstring mTitle;
	std::string mImage;
	std::mutex mButtonDataMutex; // protects mTitle, mImage

	using MediaPropertiesChanged_revoker = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession::MediaPropertiesChanged_revoker;
	using PlaybackInfoChanged_revoker = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession::PlaybackInfoChanged_revoker;

	std::map<std::string, std::tuple<MediaPropertiesChanged_revoker, PlaybackInfoChanged_revoker>> mSessionHandlers;

	winrt::Windows::Media::Control::IGlobalSystemMediaTransportControlsSessionManager mMgr{ nullptr };
};
