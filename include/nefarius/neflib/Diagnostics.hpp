#pragma once

//
// Optional, dependency-free diagnostics channel for neflib.
//
// neflib never links against a logging framework and never writes to a stream by itself; every
// public API reports failure via std::expected<T, Win32Error> (or a self-contained result struct
// for multi-step operations such as RestartDeviceInstance). That is enough for a caller to react
// to a final outcome, but it says nothing about what happened in between - which restart strategy
// was tried and rejected, whether an INF install intercepted a suppressed error dialog, whether a
// service deletion is retrying because the driver image is still in use, etc. That intermediate
// detail is exactly what a consumer wants when a user runs with a "--verbose" flag or similar.
//
// SetDiagnosticCallback lets a consumer opt into that detail without neflib taking on a hard
// dependency on any specific logging framework: register a std::function, receive DiagnosticEvent
// values, and format/dispatch them however fits (EasyLogging++, spdlog, plain std::cerr, ...). If
// no callback is registered (the default), diagnostics emission is a no-op - existing consumers
// see zero behavioral or performance change.
//
// Thread-safety / lifetime notes:
//  - The callback is a single, process-wide slot; registering a new one via SetDiagnosticCallback
//    replaces whatever was registered before. This mirrors neflib's other process-wide state
//    (e.g. the MessageBoxW/RestartDialogEx detours in Devcon.cpp) and is intended for a single
//    consumer application (nefcon, or another neflib-linked executable), not for library-internal
//    concurrent tenants.
//  - SetDiagnosticCallback/ClearDiagnosticCallback/DiagnosticsEnabled are safe to call from any
//    thread at any time, including while diagnostics are being emitted from another thread.
//  - The registered callback itself may be invoked from a worker thread: RestartDeviceInstance and
//    DetachDeviceInstance run their strategy attempts on a bounded worker thread (see
//    DeviceRestart.cpp's RunBounded) and may emit diagnostics from that thread if the attempt
//    completes before its timeout. A callback that is not itself thread-safe (e.g. one that
//    forwards to a non-thread-safe logging sink) must synchronize internally.
//  - The callback must never throw; any exception it throws is caught and discarded at the point
//    of emission so a misbehaving diagnostics sink can never affect neflib's own, otherwise
//    never-throwing, control flow.
//  - The callback must never re-enter any neflib API that itself emits diagnostics or that shares
//    process-wide state with one that does (e.g. the INF install detours' shared mutex). Treat it
//    as a leaf callback: format and forward, nothing more.
//  - DiagnosticEvent's string members are owned copies, safe to inspect after the callback returns
//    or to move into another thread/queue if a consumer wants to defer formatting.
//

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace nefarius::utilities
{
	/**
	 * Severity of a DiagnosticEvent, loosely modeled after common logging framework levels so a
	 * consumer can map it onto whatever it already uses.
	 */
	enum class DiagnosticLevel : uint8_t
	{
		///< Fine-grained, high-volume detail; only interesting with an explicit verbose/diagnostic flag
		Verbose,
		///< Notable outcome of an operation or sub-step, expected during normal operation
		Info,
		///< Non-fatal anomaly; the operation may still continue or ultimately succeed
		Warning,
		///< The operation (or sub-step) failed
		Error
	};

	/**
	 * Where in an operation's lifetime a DiagnosticEvent was raised.
	 */
	enum class DiagnosticPhase : uint8_t
	{
		///< Emitted once, right as a multi-step operation starts
		Begin,
		///< Emitted zero or more times while a multi-step operation is under way (e.g. once per
		///< restart strategy attempted)
		Progress,
		///< Emitted once, right before a multi-step operation returns successfully
		End,
		///< Emitted when an operation (or sub-step) failed
		Failure
	};

	/**
	 * A single diagnostic observation raised by a neflib API. Self-contained (every string member
	 * is an owned copy) so it can be inspected, copied, or queued after the emitting call returns.
	 */
	struct DiagnosticEvent
	{
		///< Severity of this event
		DiagnosticLevel Level = DiagnosticLevel::Info;
		///< Where in the operation's lifetime this event was raised
		DiagnosticPhase Phase = DiagnosticPhase::Progress;
		///< Stable identifier of the operation this event belongs to, e.g. "RestartDeviceInstance";
		///< always a static string literal, safe to reference without copying
		std::string_view Operation;
		///< Instance ID / service name / INF path this event refers to, if applicable; empty otherwise
		std::wstring Subject;
		///< Win32 error code associated with this event, if any
		std::optional<std::uint32_t> Win32Code;
		///< Pre-formatted, human-readable detail text
		std::string Message;
	};

	/**
	 * Signature of a diagnostics sink. See the file-level comment for thread-safety and
	 * re-entrancy requirements.
	 */
	using DiagnosticCallback = std::function<void(const DiagnosticEvent&)>;

	/**
	 * Registers (or replaces) the process-wide diagnostics sink. Pass an empty std::function (or
	 * call ClearDiagnosticCallback) to disable diagnostics again.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	03.09.2026
	 *
	 * @param 	callback	The diagnostics sink to install.
	 */
	void SetDiagnosticCallback(DiagnosticCallback callback);

	/**
	 * Removes the process-wide diagnostics sink, if any. Equivalent to
	 * SetDiagnosticCallback(nullptr).
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	03.09.2026
	 */
	void ClearDiagnosticCallback();

	/**
	 * True if a diagnostics sink is currently registered. Instrumented call sites use this
	 * internally to skip formatting a DiagnosticEvent's Message when nobody is listening; a
	 * consumer can also use it to decide whether it is worth registering a callback at all.
	 *
	 * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
	 * @date	03.09.2026
	 *
	 * @returns	True if a diagnostics sink is registered.
	 */
	bool DiagnosticsEnabled();

	//
	// Internal to neflib's own translation units. Not part of the supported public surface;
	// consumers should never call this directly.
	//
	namespace detail
	{
		void EmitDiagnostic(DiagnosticEvent event) noexcept;
	}
}
