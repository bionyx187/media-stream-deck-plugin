//==============================================================================
/**
@file       pch.h

@brief		Precompiled header

@copyright  (c) 2018, Corsair Memory, Inc.
			This source code is licensed under the MIT-style license found in the LICENSE file.

**/
//==============================================================================

#ifndef PCH_H
#define PCH_H

//-------------------------------------------------------------------
// C++ headers
//-------------------------------------------------------------------

#include <winrt/base.h>

#include <winsock2.h>
#include <Windows.h>
#include <string>
#include <set>
#include <thread>
#include <strsafe.h>

#pragma comment(lib, "windowsapp")

//-------------------------------------------------------------------
// Debug logging
//-------------------------------------------------------------------

#ifdef _DEBUG
	#define DEBUG 1
#else
	#define DEBUG 0
#endif

#define ASIO_STANDALONE

void __cdecl dbgprintf(const char *format, ...);

#if DEBUG
#define DebugPrint			dbgprintf
#else
#define DebugPrint(...)		while(0)
#endif


//-------------------------------------------------------------------
// json
//-------------------------------------------------------------------

#include "../Vendor/json/src/json.hpp"
using json = nlohmann::json;


// Configuration for event logging

#define LOG_SESSIONS 0
#define LOG_EVENTS 0
#define LOG_MESSAGES 0
#define LOG_EXCEPTIONS 1

//-------------------------------------------------------------------
// websocketpp
//-------------------------------------------------------------------

#pragma once

#endif //PCH_H
