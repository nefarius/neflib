// ReSharper disable CppRedundantQualifier
//
// Minimal, dependency-free smoke tests for the optional diagnostics API (Diagnostics.hpp /
// DiagnosticsFormat.hpp). Deliberately does not use a test framework: neflib itself has zero
// dependency on one, and pulling one in just for this small executable would be disproportionate.
//
// Scope: this only exercises the diagnostics plumbing itself (registration, emission, formatting)
// using synthetic DiagnosticEvent/DeviceRestartResult/DetachResult/ReenumerateResult values via
// detail::EmitDiagnostic - it does not touch real hardware/PnP state, which requires an elevated,
// device-carrying environment and is out of scope for an automated CI test. Those scenarios are
// covered by nefcon's manual verification checklist instead.
//
// Every check aborts the process with a non-zero exit code on failure, so this doubles as a CI
// gate (see .github/workflows/msbuild.yml).
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

#include <string>
#include <type_traits>
#include <vector>
#include <format>
#include <expected>
#include <algorithm>
#include <variant>

#include <wil/resource.h>

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
#include <nefarius/neflib/DeviceRestart.hpp>
#include <nefarius/neflib/MiscWinApi.hpp>
#include <nefarius/neflib/Diagnostics.hpp>
#include <nefarius/neflib/DiagnosticsFormat.hpp>

#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <thread>
#include <mutex>

using namespace nefarius::utilities;
using namespace nefarius::devcon;

namespace
{
	int g_failures = 0;

	void Check(bool condition, const char* expression, const char* file, int line)
	{
		if (!condition)
		{
			std::fprintf(stderr, "FAILED: %s (%s:%d)\n", expression, file, line);
			g_failures++;
		}
	}

#define CHECK(expr) ::Check((expr), #expr, __FILE__, __LINE__)

	void Test_DefaultCallbackIsNoOp()
	{
		ClearDiagnosticCallback();

		CHECK(!DiagnosticsEnabled());

		// Must not crash/throw with no callback registered.
		detail::EmitDiagnostic({
			DiagnosticLevel::Info, DiagnosticPhase::Begin, "UnitTest", L"", std::nullopt, "no listener"
		});
	}

	void Test_CallbackReceivesCopiedEvent()
	{
		std::vector<DiagnosticEvent> received;

		SetDiagnosticCallback([&received](const DiagnosticEvent& event)
		{
			received.push_back(event);
		});

		CHECK(DiagnosticsEnabled());

		detail::EmitDiagnostic({
			DiagnosticLevel::Warning, DiagnosticPhase::Failure, "UnitTest.Op", L"SomeInstanceId",
			ERROR_ACCESS_DENIED, "something failed"
		});

		CHECK(received.size() == 1);

		if (!received.empty())
		{
			CHECK(received[0].Level == DiagnosticLevel::Warning);
			CHECK(received[0].Phase == DiagnosticPhase::Failure);
			CHECK(received[0].Operation == "UnitTest.Op");
			CHECK(received[0].Subject == L"SomeInstanceId");
			CHECK(received[0].Win32Code.has_value() && received[0].Win32Code.value() == ERROR_ACCESS_DENIED);
			CHECK(received[0].Message == "something failed");
		}

		ClearDiagnosticCallback();
		CHECK(!DiagnosticsEnabled());

		// After clearing, no further events should be delivered.
		received.clear();
		detail::EmitDiagnostic({DiagnosticLevel::Info, DiagnosticPhase::Begin, "UnitTest", L"", std::nullopt, "x"});
		CHECK(received.empty());
	}

	void Test_CallbackExceptionIsSwallowed()
	{
		bool called = false;

		SetDiagnosticCallback([&called](const DiagnosticEvent&)
		{
			called = true;
			throw std::runtime_error("diagnostics sinks must never affect callers");
		});

		// Must not propagate/terminate the process.
		detail::EmitDiagnostic({DiagnosticLevel::Error, DiagnosticPhase::Failure, "UnitTest", L"", std::nullopt, ""});

		CHECK(called);

		ClearDiagnosticCallback();
	}

	void Test_ConcurrentSetAndEmitDoesNotCrash()
	{
		std::atomic<bool> stop{false};
		std::atomic<uint64_t> emitCount{0};

		std::thread emitter([&]
		{
			while (!stop.load(std::memory_order_relaxed))
			{
				detail::EmitDiagnostic({
					DiagnosticLevel::Verbose, DiagnosticPhase::Progress, "UnitTest.Concurrent", L"", std::nullopt, ""
				});
				emitCount.fetch_add(1, std::memory_order_relaxed);
			}
		});

		std::thread setter([&]
		{
			for (int i = 0; i < 2000; i++)
			{
				if (i % 2 == 0)
				{
					SetDiagnosticCallback([](const DiagnosticEvent&) {});
				}
				else
				{
					ClearDiagnosticCallback();
				}
			}
		});

		setter.join();
		stop.store(true, std::memory_order_relaxed);
		emitter.join();

		ClearDiagnosticCallback();

		CHECK(emitCount.load() > 0);
	}

	void Test_ToString_AllStrategies()
	{
		CHECK(std::string(ToString(RestartStrategy::None)) == "none");
		CHECK(std::string(ToString(RestartStrategy::UsbPortCycle)).find("USB") != std::string::npos);
		CHECK(std::string(ToString(RestartStrategy::PropertyChange)).find("property") != std::string::npos);
		CHECK(std::string(ToString(RestartStrategy::RemoveAndReenumerate)).find("re-enumerate") != std::string::npos);
	}

	void Test_DescribeDeviceRestartResult_Success()
	{
		DeviceRestartResult result;
		result.InstanceId = L"ROOT\\TEST\\0001";
		result.FriendlyName = L"Test Device";
		result.Strategy = RestartStrategy::PropertyChange;
		result.Succeeded = true;
		result.LastError = ERROR_SUCCESS;

		const std::string description = DescribeDeviceRestartResult(result);

		CHECK(description.find("Test Device") != std::string::npos);
		CHECK(description.find("property change") != std::string::npos);
	}

	void Test_DescribeDeviceRestartResult_Veto()
	{
		DeviceRestartResult result;
		result.InstanceId = L"ROOT\\TEST\\0002";
		result.Succeeded = false;
		result.DevicePresent = true;
		result.FinalStatusValid = true;
		result.FinalStarted = false;
		result.FinalHasProblem = true;
		result.FinalProblemCode = CM_PROB_FAILED_INSTALL;
		result.LastAttempted = RestartStrategy::RemoveAndReenumerate;
		result.LastError = ERROR_CANCELLED;
		result.VetoName = L"SomeDriver.sys";
		result.VetoType = PNP_VetoDevice;

		const std::string description = DescribeDeviceRestartResult(result);

		CHECK(description.find("SomeDriver.sys") != std::string::npos);
		CHECK(description.find("CM_PROB_FAILED_INSTALL") != std::string::npos);
	}

	void Test_DescribeDeviceRestartResult_NotPresent()
	{
		DeviceRestartResult result;
		result.InstanceId = L"ROOT\\TEST\\0003";
		result.Succeeded = false;
		result.DevicePresent = false;

		const std::string description = DescribeDeviceRestartResult(result);

		CHECK(description.find("no longer present") != std::string::npos);
	}

	void Test_DescribeDetachResult()
	{
		DetachResult success;
		success.InstanceId = L"ROOT\\TEST\\0004";
		success.Succeeded = true;

		CHECK(DescribeDetachResult(success).find("Detached") != std::string::npos);

		DetachResult vetoed;
		vetoed.InstanceId = L"ROOT\\TEST\\0005";
		vetoed.Succeeded = false;
		vetoed.VetoName = L"Blocker.exe";
		vetoed.LastError = ERROR_CANCELLED;

		CHECK(DescribeDetachResult(vetoed).find("Blocker.exe") != std::string::npos);
	}

	void Test_DescribeReenumerateResult()
	{
		ReenumerateResult timedOut;
		timedOut.InstanceId = L"ROOT\\TEST\\0006";
		timedOut.TimedOut = true;
		timedOut.LastError = ERROR_TIMEOUT;

		CHECK(DescribeReenumerateResult(timedOut).find("Timed out") != std::string::npos);
	}
}

int main()
{
	Test_DefaultCallbackIsNoOp();
	Test_CallbackReceivesCopiedEvent();
	Test_CallbackExceptionIsSwallowed();
	Test_ConcurrentSetAndEmitDoesNotCrash();
	Test_ToString_AllStrategies();
	Test_DescribeDeviceRestartResult_Success();
	Test_DescribeDeviceRestartResult_Veto();
	Test_DescribeDeviceRestartResult_NotPresent();
	Test_DescribeDetachResult();
	Test_DescribeReenumerateResult();

	if (g_failures == 0)
	{
		std::printf("All neflib diagnostics tests passed.\n");
		return 0;
	}

	std::fprintf(stderr, "%d neflib diagnostics test(s) failed.\n", g_failures);
	return 1;
}
