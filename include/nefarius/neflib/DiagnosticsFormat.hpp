#pragma once

//
// Human-readable formatting helpers for the multi-step result structs declared in
// DeviceRestart.hpp. Split out from Diagnostics.hpp since these are plain value->string
// conversions (no callback/global state involved) that a consumer can use regardless of whether
// it has registered a diagnostics callback - e.g. to log DeviceRestartResult once at an API
// boundary without wiring up SetDiagnosticCallback at all.
//

#include <string>

#include <nefarius/neflib/DeviceRestart.hpp>

namespace nefarius::devcon
{
	/**
	 * Human-readable name of a RestartStrategy value, e.g. "USB port cycle".
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	03.09.2026
	 */
	const char* ToString(RestartStrategy strategy) noexcept;

	/**
	 * Human-readable name of a CM_PROB_* problem code. Only the codes plausible for a device that
	 * just went through a restart ladder are named; anything else is reported as its raw numeric
	 * value so nothing is hidden.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	03.09.2026
	 */
	std::string ProblemCodeToString(ULONG problemCode);

	/**
	 * Renders the FinalStatusValid/FinalStarted/FinalHasProblem/FinalProblemCode fields of a
	 * DeviceRestartResult as a short, parenthesized clause suitable for appending to a log line,
	 * e.g. " (final devnode state: started=true, no problem code)". Lets a caller distinguish a
	 * device that is genuinely stuck (has a problem code) from one that simply took slightly
	 * longer than a single strategy's verify window.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	03.09.2026
	 */
	std::string DescribeFinalDevNodeState(const DeviceRestartResult& result);

	/**
	 * Renders a full, single-line human-readable summary of a DeviceRestartResult, suitable for a
	 * single log line describing the outcome of RestartDeviceInstance.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	03.09.2026
	 */
	std::string DescribeDeviceRestartResult(const DeviceRestartResult& result);

	/**
	 * Renders a full, single-line human-readable summary of a DetachResult, suitable for a single
	 * log line describing the outcome of DetachDeviceInstance.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	03.09.2026
	 */
	std::string DescribeDetachResult(const DetachResult& result);

	/**
	 * Renders a full, single-line human-readable summary of a ReenumerateResult, suitable for a
	 * single log line describing the outcome of ReenumerateParentDevNode.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	03.09.2026
	 */
	std::string DescribeReenumerateResult(const ReenumerateResult& result);
}
