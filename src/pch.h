#pragma once

//
// Include WinAPI stuff
// 
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <ShlObj.h>
#include <SetupAPI.h>
#include <newdev.h>
#include <tchar.h>
#include <initguid.h>
#include <devguid.h>
#include <shellapi.h>
#include <TlHelp32.h>
#include <cfgmgr32.h>

//
// Include consumed STL
// 
#include <string>
#include <type_traits>
#include <vector>
#include <format>
#include <expected>
#include <algorithm>
#include <variant>

//
// Vcpkg dependencies
// 
#include <wil/resource.h>
#include <detours/detours.h>
#include <scope_guard.hpp>

//
// Internal stuff
// 
#include "ScopeGuardHelper.hpp"

//
// Public headers
// 
#include <nefarius/neflib/AnyString.hpp>
#include <nefarius/neflib/UniUtil.hpp>
#include <nefarius/neflib/HDEVINFOHandleGuard.hpp>
#include <nefarius/neflib/HKEYHandleGuard.hpp>
#include <nefarius/neflib/INFHandleGuard.hpp>
#include <nefarius/neflib/GenHandleGuard.hpp>
#include <nefarius/neflib/LibraryHelper.hpp>
#include <nefarius/neflib/MultiStringArray.hpp>
#include <nefarius/neflib/Win32Error.hpp>
#include <nefarius/neflib/ClassFilter.hpp>
#include <nefarius/neflib/Devcon.hpp>
#include <nefarius/neflib/MiscWinApi.hpp>
