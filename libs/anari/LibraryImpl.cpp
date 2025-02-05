// Copyright 2021 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "anari/backend/LibraryImpl.h"

// std
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <sys/times.h>
#endif

#include <stdexcept>
#include <vector>

#if defined(__MACOSX__) || defined(__APPLE__)
#define RKCOMMON_LIB_EXT ".dylib"
#else
#define RKCOMMON_LIB_EXT ".so"
#endif

#ifdef _WIN32
#define LOOKUP_SYM(lib, symbol) GetProcAddress((HMODULE)lib, symbol.c_str());
#define FREE_LIB(lib) FreeLibrary((HMODULE)lib);
#else
#define LOOKUP_SYM(lib, symbol) dlsym(lib, symbol.c_str());
#define FREE_LIB(lib) dlclose(lib)
#endif

/* Export a symbol to ask the dynamic loader about in order to locate this
 * library at runtime. */
extern "C" int _anari_anchor()
{
  return 0;
}

namespace {

std::string library_location()
{
#if defined(_WIN32) && !defined(__CYGWIN__)
  MEMORY_BASIC_INFORMATION mbi;
  VirtualQuery(&_anari_anchor, &mbi, sizeof(mbi));
  char pathBuf[16384];
  if (!GetModuleFileNameA(
          static_cast<HMODULE>(mbi.AllocationBase), pathBuf, sizeof(pathBuf)))
    return std::string();

  std::string path = std::string(pathBuf);
  path.resize(path.rfind('\\') + 1);
#else
  const char *anchor = "_anari_anchor";
  void *handle = dlsym(RTLD_DEFAULT, anchor);
  if (!handle)
    return std::string();

  Dl_info di;
  int ret = dladdr(handle, &di);
  if (!ret || !di.dli_saddr || !di.dli_fname)
    return std::string();

  std::string path = std::string(di.dli_fname);
  path.resize(path.rfind('/') + 1);
#endif

  return path;
}

} // namespace

namespace anari {

using InitFcn = void (*)();
using AllocFcn = void *(*)();

static std::vector<void *> g_loadedLibs;

static void *loadLibrary(
    const std::string &libName, bool withAnchor, std::string &errorMessage)
{
  std::string file = libName;
  std::string errorMsg;
  std::string libLocation = withAnchor ? library_location() : std::string();
  void *lib = nullptr;
#ifdef _WIN32
  // Set cwd to library location, to make sure dependent libraries are found as
  // well
  constexpr int MAX_DIRSIZE = 4096;
  TCHAR currentWd[MAX_DIRSIZE];
  DWORD dwRet = 0;
  if (withAnchor)
    dwRet = GetCurrentDirectory(MAX_DIRSIZE, currentWd);

  if (dwRet > MAX_DIRSIZE)
    errorMsg = "library path larger than " + std::to_string(MAX_DIRSIZE)
        + " characters";
  else if (withAnchor && dwRet == 0)
    errorMsg = "GetCurrentDirectory() failed for unknown reason";
  else {
    SetCurrentDirectory(libLocation.c_str());

    // Load the library
    std::string fullName = libLocation + file + ".dll";
    lib = LoadLibrary(fullName.c_str());
    if (lib == nullptr) {
      DWORD err = GetLastError();
      LPTSTR lpMsgBuf;
      FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
              | FORMAT_MESSAGE_IGNORE_INSERTS,
          NULL,
          err,
          MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
          (LPTSTR)&lpMsgBuf,
          0,
          NULL);

      errorMsg = lpMsgBuf;

      LocalFree(lpMsgBuf);
    }

    // Change cwd back to its original value
    SetCurrentDirectory(currentWd);
  }

#else
  std::string fullName = libLocation + "lib" + file + RKCOMMON_LIB_EXT;
  lib = dlopen(fullName.c_str(), RTLD_LAZY | RTLD_LOCAL);
  if (lib == nullptr) {
    errorMsg += dlerror();
  }
#endif

  if (lib == nullptr) {
    errorMessage += " could not open library lib " + libName + ": " + errorMsg;
  }

  return lib;
}

void *loadANARILibrary(const std::string &libName)
{
  std::string errorMessage;

  void *lib = loadLibrary(libName, false, errorMessage);
  if (!lib) {
    errorMessage = "(unanchored library load attempt failed)\n";
    lib = loadLibrary(libName, true, errorMessage);
  }

  if (!lib)
    throw std::runtime_error(errorMessage);

  return lib;
}

void *getSymbolAddress(void *lib, const std::string &symbol)
{
  return LOOKUP_SYM(lib, symbol);
}

void freeLibrary(void *lib)
{
  if (lib)
    FREE_LIB(lib);
}

///////////////////////////////////////////////////////////////////////////////
// LibraryImpl definitions ////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

LibraryImpl::LibraryImpl(const char *name,
    ANARIStatusCallback defaultStatusCB,
    const void *statusCBPtr)
    : m_defaultDeviceName(name),
      m_defaultStatusCB(defaultStatusCB),
      m_defaultStatusCBUserPtr(statusCBPtr)
{
  void *lib = loadANARILibrary(std::string("anari_library_") + name);

  if (!lib)
    throw std::runtime_error("failed to load library " + std::string(name));

  m_lib = lib;

  std::string prefix = "anari_library_" + std::string(name);

  std::string newDeviceFcnName = prefix + "_new_device";
  m_newDeviceFcn = (NewDeviceFcn)getSymbolAddress(m_lib, newDeviceFcnName);

  if (!m_newDeviceFcn) {
    throw std::runtime_error("failed to find newDevice() function for "
        + std::string(name) + " library");
  }

  std::string initFcnName = prefix + "_init";
  auto initFcn = (InitFcn)getSymbolAddress(m_lib, initFcnName);

  if (initFcn)
    initFcn();

  std::string allocFcnName = prefix + "_allocate";
  AllocFcn allocFcn = (AllocFcn)getSymbolAddress(m_lib, allocFcnName);

  if (allocFcn)
    m_libraryData = allocFcn();

  std::string freeFcnName = prefix + "_free";
  FreeFcn freeFcn = (FreeFcn)getSymbolAddress(m_lib, freeFcnName);

  if (freeFcn)
    m_freeFcn = freeFcn;

  std::string loadModuleFcnName = prefix + "_load_module";
  auto loadModuleFcn = (ModuleFcn)getSymbolAddress(m_lib, loadModuleFcnName);

  if (loadModuleFcn)
    m_loadModuleFcn = loadModuleFcn;

  std::string unloadModuleFcnName = prefix + "_unload_module";
  auto unloadModuleFcn =
      (ModuleFcn)getSymbolAddress(m_lib, unloadModuleFcnName);

  if (unloadModuleFcn)
    m_unloadModuleFcn = unloadModuleFcn;

  std::string getDeviceSubtypesFcnName = prefix + "_get_device_subtypes";
  auto getDeviceSubtypesFcn =
      (GetDeviceSubtypesFcn)getSymbolAddress(m_lib, getDeviceSubtypesFcnName);

  if (getDeviceSubtypesFcn)
    m_getDeviceSubtypesFcn = getDeviceSubtypesFcn;

  std::string getDeviceFeaturesFcnName = prefix + "_get_device_features";
  auto getDeviceFeaturesFcn =
      (GetDeviceFeaturesFcn)getSymbolAddress(m_lib, getDeviceFeaturesFcnName);

  if (getDeviceFeaturesFcn)
    m_getDeviceFeaturesFcn = getDeviceFeaturesFcn;

  auto devices = getDeviceSubtypes();
  if (devices && devices[0])
    m_defaultDeviceName = devices[0];
}

LibraryImpl::~LibraryImpl()
{
  if (m_freeFcn)
    m_freeFcn(m_libraryData);

  freeLibrary(m_lib);
}

void *LibraryImpl::libraryData() const
{
  return m_libraryData;
}

ANARIDevice LibraryImpl::newDevice(const char *subtype) const
{
  return m_newDeviceFcn ? m_newDeviceFcn((ANARILibrary)this, subtype) : nullptr;
}

const char *LibraryImpl::defaultDeviceName() const
{
  return m_defaultDeviceName.c_str();
}

ANARIStatusCallback LibraryImpl::defaultStatusCB() const
{
  return m_defaultStatusCB;
}

const void *LibraryImpl::defaultStatusCBUserPtr() const
{
  return m_defaultStatusCBUserPtr;
}

void LibraryImpl::loadModule(const char *name) const
{
  if (m_loadModuleFcn)
    m_loadModuleFcn((ANARILibrary)this, name);
}

void LibraryImpl::unloadModule(const char *name) const
{
  if (m_unloadModuleFcn)
    m_unloadModuleFcn((ANARILibrary)this, name);
}

const char **LibraryImpl::getDeviceSubtypes()
{
  if (m_getDeviceSubtypesFcn)
    return m_getDeviceSubtypesFcn((ANARILibrary)this);
  return nullptr;
}

const char **LibraryImpl::getDeviceFeatures(const char *deviceType)
{
  if (m_getDeviceFeaturesFcn)
    return m_getDeviceFeaturesFcn((ANARILibrary)this, deviceType);
  return nullptr;
}

ANARI_TYPEFOR_DEFINITION(LibraryImpl *);

} // namespace anari
