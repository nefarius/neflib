# <img src="assets/NSS-128x128.png" align="left" />neflib

[![MSBuild](https://github.com/nefarius/neflib/actions/workflows/msbuild.yml/badge.svg)](https://github.com/nefarius/neflib/actions/workflows/msbuild.yml)
![Requirements](https://img.shields.io/badge/Requires-C++23-blue.svg)

My opinionated collection of C++ utilities.

Work in progress 🔥

## How to use

I recommend you use [vcpkg](https://github.com/microsoft/vcpkg) to consume the library, alternatively you can just clone, build and link against the resulting static library.

### Library

To grab and built it automatically via package manager first create a `vcpkg-configuration.json` containing:

```json
{
  "registries": [
    {
      "kind": "git",
      "repository": "https://github.com/nefarius/nefarius-vcpkg-registry.git",
      "baseline": "e86303f5790f4867f9be729e001477f932853871",
      "packages": [ "neflib" ]
    }
  ],
  "default-registry": {
    "kind": "builtin",
    "baseline": "2024.07.12"
  }
}
```

This will make vcpkg aware of [my own package registry](https://github.com/nefarius/nefarius-vcpkg-registry) in addition to the built-in one. The `baseline` tags and hashes might become outdated fast so look them up before copying them verbatim.

Now create a `vcpkg.json` manifest for your project similar to this:

```json
{
  "name": "demoproject",
  "version": "1.0.0",
  "description": "demoproject",
  "license": "MIT",
  "supports": "!(arm | uwp)",
  "dependencies": [
    "neflib",
    "wil"
  ]
}
```

After both these files are placed in the project directory, make sure to tell VS to use the manifest and linking options:

![XFOywDXdHv.png](assets/XFOywDXdHv.png)

Now each time you build, the latest public version of `neflib` will be pulled, built and linked with your project 💪

### Includes

My philosophy is having my code stay out of your project structures' and preferences way as much as possible. I dislike libraries that force-include a ton of additional headers hidden away from you which might cause conflicts in bigger projects. This is the *recommended* minimal requirement and order of additional includes (OS APIs, STL etc.) you will need to use the full feature set of the library. You may get away with less if you only use a sub-set of the library:

```cpp
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

//
// Include consumed STL
// 
#include <string>
#include <type_traits>
#include <vector>
#include <format>
#include <expected>
#include <sstream>

//
// Vcpkg dependencies
// 
#include <wil/resource.h>

//
// Public headers
// 
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
```

This approach is also compatible with the use of precompiled headers, if you desire to use the feature. Optional, ofc. 😎

## Sources and 3rd party credits

- [Windows Implementation Library](https://github.com/microsoft/wil)
- [Microsoft Research Detours Package](https://github.com/microsoft/Detours)
- [A modern C++ scope guard that is easy to use but hard to misuse](https://github.com/ricab/scope_guard)
