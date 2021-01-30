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

#include <winrt/base.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Foundation.Collections.h>

using namespace winrt;
using namespace Windows::Media::Control;

class CpuUsageHelper;
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

private:
	
	void UpdateTimer();
	void CheckMedia();
	std::string utf8_encode(const std::wstring& wstr);
	
	std::mutex mVisibleContextsMutex;
	std::mutex mDataMutex;
	std::set<std::string> mVisibleContexts;
	
	CpuUsageHelper *mCpuUsageHelper = nullptr;
	CallBackTimer* mDisplayTimer;
	CallBackTimer *mMediaCheckTimer;
	unsigned int mTicks;
	int mTextWidth;
	winrt::hstring mTitle;
};
