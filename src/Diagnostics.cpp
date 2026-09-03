#include "pch.h"

#include <memory>
#include <mutex>

#include <nefarius/neflib/Diagnostics.hpp>

namespace
{
	std::mutex g_diagnosticsMutex;
	// Held by shared_ptr so EmitDiagnostic can take a local copy of the current callback under the
	// lock and then invoke it outside the lock, without racing a concurrent SetDiagnosticCallback/
	// ClearDiagnosticCallback call on another thread.
	std::shared_ptr<nefarius::utilities::DiagnosticCallback> g_diagnosticCallback;
}

void nefarius::utilities::SetDiagnosticCallback(DiagnosticCallback callback)
{
	auto next = callback
		            ? std::make_shared<DiagnosticCallback>(std::move(callback))
		            : std::shared_ptr<DiagnosticCallback>();

	std::lock_guard lock(g_diagnosticsMutex);
	g_diagnosticCallback = std::move(next);
}

void nefarius::utilities::ClearDiagnosticCallback()
{
	std::lock_guard lock(g_diagnosticsMutex);
	g_diagnosticCallback.reset();
}

bool nefarius::utilities::DiagnosticsEnabled()
{
	std::lock_guard lock(g_diagnosticsMutex);
	return static_cast<bool>(g_diagnosticCallback);
}

void nefarius::utilities::detail::EmitDiagnostic(DiagnosticEvent event) noexcept
{
	std::shared_ptr<DiagnosticCallback> callback;

	{
		std::lock_guard lock(g_diagnosticsMutex);
		callback = g_diagnosticCallback;
	}

	if (!callback)
	{
		return;
	}

	try
	{
		(*callback)(event);
	}
	catch (...)
	{
		//
		// A diagnostics sink must never be able to affect neflib's own control flow (most of the
		// APIs that emit diagnostics are documented as never-throwing). Swallow anything it throws.
		//
	}
}
