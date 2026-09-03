// ReSharper disable CppRedundantQualifier
//
// Minimal, dependency-free tests for the driver store matching logic added alongside the
// FindDriverStorePackages/RemoveDriverStorePackages API (GetOriginalInfNameOfStorePackage's
// FileRepository directory-name parsing, and DriverStorePackageMatchesFilter's pure AND-matching
// semantics). Deliberately does not use a test framework, matching DiagnosticsTests.cpp.
//
// Scope: GetOriginalInfNameOfStorePackage is pure string parsing and DriverStorePackageMatchesFilter
// is a pure predicate over already-resolved DriverStorePackageDetails, so both are fully testable
// with synthetic data - no real driver store, elevation, or hardware required. The I/O-dependent
// parts (EnumerateDriverStorePackages, ReadDriverStoreIdentityFromInf, GetINFClass,
// GetInfClassFilterTargets, and the actual deletion cascade) are exercised only by nefcon's manual
// verification checklist, which has a real driver store with genuine multi-version packages to
// check against.
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

#include <cstdio>
#include <cstdlib>

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

	// Two distinct GUIDs used across the filter-matching tests below; values themselves are
	// arbitrary, only their identity/inequality matters.
	constexpr GUID GuidA = {0x12345678, 0x1234, 0x1234, {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0}};
	constexpr GUID GuidB = {0x87654321, 0x4321, 0x4321, {0x21, 0x43, 0x65, 0x87, 0x09, 0xba, 0xdc, 0xfe}};

	//
	// GetOriginalInfNameOfStorePackage - pure FileRepository directory-name parsing.
	//

	void Test_OriginalInfName_FullPathWithTrailingInfFile()
	{
		const auto name = GetOriginalInfNameOfStorePackage(
			LR"(C:\Windows\System32\DriverStore\FileRepository\example1.inf_amd64_5ca6d479976bcd98\example1.inf)");

		CHECK(name.has_value());
		if (name)
		{
			CHECK(*name == L"example1.inf");
		}
	}

	void Test_OriginalInfName_DirectoryOnlyPath()
	{
		// No trailing "\<name>.inf" component - the path already ends at the package directory.
		const auto name = GetOriginalInfNameOfStorePackage(
			LR"(C:\Windows\System32\DriverStore\FileRepository\example2.inf_amd64_deadbeef1234)");

		CHECK(name.has_value());
		if (name)
		{
			CHECK(*name == L"example2.inf");
		}
	}

	void Test_OriginalInfName_GreedyLastSeparator()
	{
		// The original file's own name contains ".inf_", which a non-greedy (first-match) search
		// would truncate at; the real separator is the LAST ".inf_" in the directory name.
		const auto name = GetOriginalInfNameOfStorePackage(
			LR"(C:\Store\FileRepository\weird.inf_backup.inf_amd64_hash\weird.inf_backup.inf)");

		CHECK(name.has_value());
		if (name)
		{
			CHECK(*name == L"weird.inf_backup.inf");
		}
	}

	void Test_OriginalInfName_CaseInsensitiveSeparatorSearch()
	{
		// The ".inf_" separator search must be case-insensitive even though the returned name
		// preserves whatever case the directory itself used.
		const auto name = GetOriginalInfNameOfStorePackage(
			LR"(C:\Store\FileRepository\MyDrv.INF_amd64_hash)");

		CHECK(name.has_value());
		if (name)
		{
			CHECK(*name == L"MyDrv.INF");
		}
	}

	void Test_OriginalInfName_NoBackslash_ReturnsNullopt()
	{
		CHECK(!GetOriginalInfNameOfStorePackage(L"justname.inf_amd64_hash").has_value());
	}

	void Test_OriginalInfName_NoSeparatorInDirectoryName_ReturnsNullopt()
	{
		// Directory name has no ".inf_" at all (e.g. a driver store layout this parser doesn't
		// understand) - must fail closed rather than guess.
		CHECK(!GetOriginalInfNameOfStorePackage(LR"(C:\Store\FileRepository\justname)").has_value());
	}

	void Test_OriginalInfName_InsufficientPathDepth_ReturnsNullopt()
	{
		// After stripping the trailing "<name>.inf" component, there is no further directory
		// component left to parse.
		CHECK(!GetOriginalInfNameOfStorePackage(LR"(C:\SomeFile.inf)").has_value());
	}

	void Test_OriginalInfName_EmptyPrefix_ReturnsNullopt()
	{
		// The ".inf_" separator is right at the start of the directory name, leaving no original
		// name prefix at all.
		CHECK(!GetOriginalInfNameOfStorePackage(LR"(C:\Store\FileRepository\.inf_amd64_hash)").has_value());
	}

	//
	// DriverStorePackageMatchesFilter - pure AND-matching over synthetic package details.
	//

	DriverStorePackageDetails MakeDetails(const wchar_t* originalInfName, const wchar_t* provider,
	                                      const wchar_t* driverVer, const GUID& classGuid,
	                                      std::vector<std::wstring> serviceNames)
	{
		DriverStorePackageDetails details;
		details.DriverPackageInfPath = L"C:\\Store\\FileRepository\\synthetic";
		details.PublishedInfName = L"oem1.inf";
		details.OriginalInfName = originalInfName;
		details.Provider = provider;
		details.DriverVer = driverVer;
		details.ClassGuid = classGuid;
		details.ServiceNames = std::move(serviceNames);
		return details;
	}

	void Test_MatchesFilter_EmptyFilterMatchesEverything()
	{
		// Vacuously true - FindDriverStorePackages is what actually refuses an empty filter (see
		// Test_FindDriverStorePackages_RejectsEmptyFilter), not this predicate.
		const auto details = MakeDetails(L"example.inf", L"Contoso", L"1.0.0.1", GuidA, {L"svc1"});
		CHECK(DriverStorePackageMatchesFilter(details, DriverStorePackageFilter{}));
	}

	void Test_MatchesFilter_OriginalInfNameCaseInsensitive()
	{
		const auto details = MakeDetails(L"Example.INF", L"Contoso", L"1.0.0.1", GuidA, {});

		DriverStorePackageFilter filter;
		filter.OriginalInfNames = {L"example.inf"};

		CHECK(DriverStorePackageMatchesFilter(details, filter));

		filter.OriginalInfNames = {L"unrelated.inf"};
		CHECK(!DriverStorePackageMatchesFilter(details, filter));
	}

	void Test_MatchesFilter_OriginalInfNameAnyOfList()
	{
		const auto details = MakeDetails(L"b.inf", L"Contoso", L"1.0.0.1", GuidA, {});

		DriverStorePackageFilter filter;
		filter.OriginalInfNames = {L"a.inf", L"b.inf", L"c.inf"};

		CHECK(DriverStorePackageMatchesFilter(details, filter));
	}

	void Test_MatchesFilter_ProviderAndDriverVerCaseInsensitiveExact()
	{
		const auto details = MakeDetails(L"example.inf", L"Contoso Inc.", L"3.4.5.6", GuidA, {});

		DriverStorePackageFilter filter;
		filter.Provider = L"CONTOSO INC.";
		filter.DriverVer = L"3.4.5.6";
		CHECK(DriverStorePackageMatchesFilter(details, filter));

		filter.DriverVer = L"9.9.9.9";
		CHECK(!DriverStorePackageMatchesFilter(details, filter));
	}

	void Test_MatchesFilter_ClassGuidMustMatchExactly()
	{
		const auto details = MakeDetails(L"example.inf", L"Contoso", L"1.0.0.1", GuidA, {});

		DriverStorePackageFilter filter;
		filter.ClassGuid = GuidA;
		CHECK(DriverStorePackageMatchesFilter(details, filter));

		filter.ClassGuid = GuidB;
		CHECK(!DriverStorePackageMatchesFilter(details, filter));
	}

	void Test_MatchesFilter_ServiceNameAnyOfCandidatesServices()
	{
		const auto details = MakeDetails(L"example.inf", L"Contoso", L"1.0.0.1", GuidA,
		                                 {L"UpperFilterSvc", L"LowerFilterSvc"});

		DriverStorePackageFilter filter;
		filter.ServiceName = L"lowerfiltersvc"; // case-insensitive
		CHECK(DriverStorePackageMatchesFilter(details, filter));

		filter.ServiceName = L"NoSuchService";
		CHECK(!DriverStorePackageMatchesFilter(details, filter));
	}

	void Test_MatchesFilter_UnresolvedFieldIsNeverAPassThrough()
	{
		// A candidate for which Provider/DriverVer/ClassGuid/ServiceNames could not be determined
		// (e.g. the INF couldn't be opened) leaves those fields unset on Details. A filter that
		// populates any of those criteria must reject such a candidate, never treat the missing
		// data as satisfying the criterion.
		DriverStorePackageDetails details;
		details.DriverPackageInfPath = L"C:\\Store\\FileRepository\\synthetic";
		details.OriginalInfName = L"example.inf";
		// Provider, DriverVer, ClassGuid, ServiceNames deliberately left unset/empty.

		DriverStorePackageFilter providerFilter;
		providerFilter.Provider = L"Contoso";
		CHECK(!DriverStorePackageMatchesFilter(details, providerFilter));

		DriverStorePackageFilter classFilter;
		classFilter.ClassGuid = GuidA;
		CHECK(!DriverStorePackageMatchesFilter(details, classFilter));

		DriverStorePackageFilter serviceFilter;
		serviceFilter.ServiceName = L"AnyService";
		CHECK(!DriverStorePackageMatchesFilter(details, serviceFilter));
	}

	void Test_MatchesFilter_MultiVersionSet_AllVersionsFilterMatchesEveryVersion()
	{
		// Simulates re-published copies of the same driver (same class + filter service + original
		// INF name, different Provider/DriverVer per version) - the shape --all-versions purge
		// relies on: a filter with no Provider/DriverVer must match every version.
		const std::vector<DriverStorePackageDetails> versions = {
			MakeDetails(L"contoso.inf", L"Contoso", L"1.0.0.1", GuidA, {L"ContosoFilter"}),
			MakeDetails(L"contoso.inf", L"Contoso", L"2.0.0.1", GuidA, {L"ContosoFilter"}),
			MakeDetails(L"contoso.inf", L"Contoso", L"3.0.0.1", GuidA, {L"ContosoFilter"}),
		};

		DriverStorePackageFilter allVersionsFilter;
		allVersionsFilter.ClassGuid = GuidA;
		allVersionsFilter.ServiceName = L"ContosoFilter";
		allVersionsFilter.OriginalInfNames = {L"contoso.inf"};

		const auto matchCount = std::ranges::count_if(versions, [&](const DriverStorePackageDetails& d)
		{
			return DriverStorePackageMatchesFilter(d, allVersionsFilter);
		});

		CHECK(matchCount == static_cast<std::ptrdiff_t>(versions.size()));
	}

	void Test_MatchesFilter_MultiVersionSet_ExactFilterMatchesOnlyOneVersion()
	{
		const std::vector<DriverStorePackageDetails> versions = {
			MakeDetails(L"contoso.inf", L"Contoso", L"1.0.0.1", GuidA, {L"ContosoFilter"}),
			MakeDetails(L"contoso.inf", L"Contoso", L"2.0.0.1", GuidA, {L"ContosoFilter"}),
			MakeDetails(L"contoso.inf", L"Contoso", L"3.0.0.1", GuidA, {L"ContosoFilter"}),
		};

		DriverStorePackageFilter exactFilter;
		exactFilter.OriginalInfNames = {L"contoso.inf"};
		exactFilter.Provider = L"Contoso";
		exactFilter.DriverVer = L"2.0.0.1";

		const auto matchCount = std::ranges::count_if(versions, [&](const DriverStorePackageDetails& d)
		{
			return DriverStorePackageMatchesFilter(d, exactFilter);
		});

		CHECK(matchCount == 1);
	}

	void Test_MatchesFilter_UnrelatedPackageSharingNameIsExcludedByClassAndService()
	{
		// Two unrelated packages that happen to share an original INF basename (e.g. a generic
		// name reused by different vendors) must be distinguishable via class + service even
		// though the name alone matches both.
		const auto ours = MakeDetails(L"driver.inf", L"Contoso", L"1.0.0.1", GuidA, {L"ContosoFilter"});
		const auto unrelated = MakeDetails(L"driver.inf", L"OtherVendor", L"9.9.9.9", GuidB, {L"OtherFilter"});

		DriverStorePackageFilter filter;
		filter.OriginalInfNames = {L"driver.inf"};
		filter.ClassGuid = GuidA;
		filter.ServiceName = L"ContosoFilter";

		CHECK(DriverStorePackageMatchesFilter(ours, filter));
		CHECK(!DriverStorePackageMatchesFilter(unrelated, filter));
	}

	//
	// FindDriverStorePackages - only the pure, no-I/O upfront validation is testable without a
	// real driver store; a fully empty filter must be rejected before anything is enumerated.
	//

	void Test_FindDriverStorePackages_RejectsEmptyFilter()
	{
		const auto result = FindDriverStorePackages(DriverStorePackageFilter{});

		CHECK(!result.has_value());
		if (!result)
		{
			CHECK(result.error().getErrorCode() == ERROR_INVALID_PARAMETER);
		}
	}
}

//
// Entry point called from DiagnosticsTests.cpp's main() - this file intentionally does not
// define its own main() since both translation units link into the single neflib-tests.exe.
// Returns the number of failed checks (0 = all passed).
//
int RunDriverStoreTests()
{
	Test_OriginalInfName_FullPathWithTrailingInfFile();
	Test_OriginalInfName_DirectoryOnlyPath();
	Test_OriginalInfName_GreedyLastSeparator();
	Test_OriginalInfName_CaseInsensitiveSeparatorSearch();
	Test_OriginalInfName_NoBackslash_ReturnsNullopt();
	Test_OriginalInfName_NoSeparatorInDirectoryName_ReturnsNullopt();
	Test_OriginalInfName_InsufficientPathDepth_ReturnsNullopt();
	Test_OriginalInfName_EmptyPrefix_ReturnsNullopt();

	Test_MatchesFilter_EmptyFilterMatchesEverything();
	Test_MatchesFilter_OriginalInfNameCaseInsensitive();
	Test_MatchesFilter_OriginalInfNameAnyOfList();
	Test_MatchesFilter_ProviderAndDriverVerCaseInsensitiveExact();
	Test_MatchesFilter_ClassGuidMustMatchExactly();
	Test_MatchesFilter_ServiceNameAnyOfCandidatesServices();
	Test_MatchesFilter_UnresolvedFieldIsNeverAPassThrough();
	Test_MatchesFilter_MultiVersionSet_AllVersionsFilterMatchesEveryVersion();
	Test_MatchesFilter_MultiVersionSet_ExactFilterMatchesOnlyOneVersion();
	Test_MatchesFilter_UnrelatedPackageSharingNameIsExcludedByClassAndService();

	Test_FindDriverStorePackages_RejectsEmptyFilter();

	if (g_failures == 0)
	{
		std::printf("All neflib driver store tests passed.\n");
	}
	else
	{
		std::fprintf(stderr, "%d neflib driver store test(s) failed.\n", g_failures);
	}

	return g_failures;
}
