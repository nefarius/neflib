#pragma once

//
// Include WinAPI stuff
// 
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <SetupAPI.h>

//
// Include consumed STL
// 
#include <string>

//
// Public headers
// 
#include <nefarius/neflib/UniUtil.hpp>

#include <nefarius/neflib/HDEVINFOHandleGuard.hpp>
#include <nefarius/neflib/HKEYHandleGuard.hpp>
#include <nefarius/neflib/INFHandleGuard.hpp>
