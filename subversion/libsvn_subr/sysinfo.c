/*
 * sysinfo.c :  information about the running system
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */



#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#define PSAPI_VERSION 1
#include <windows.h>
#include <psapi.h>
#endif

#define APR_WANT_STRFUNC
#include <apr_want.h>

#include <apr_lib.h>
#include <apr_pools.h>
#include <apr_file_info.h>

#include "svn_ctype.h"
#include "svn_error.h"
#include "svn_utf.h"

#include "sysinfo.h"
#include "svn_private_config.h"

#if HAVE_SYS_UTSNAME_H
#include <sys/utsname.h>
#endif

#if HAVE_UNAME
static const char* canonical_host_from_uname(apr_pool_t *pool);
#endif

#ifdef WIN32
static const char * win32_canonical_host(apr_pool_t *pool);
static const char * win32_release_name(apr_pool_t *pool);
static const char * win32_shared_libs(apr_pool_t *pool);
#endif /* WIN32 */


const char *
svn_sysinfo__canonical_host(apr_pool_t *pool)
{
#if HAVE_UNAME
  return canonical_host_from_uname(pool);
#elif defined(WIN32)
  return win32_canonical_host(pool);
#else
  return "unknown-unknown-unknown";
#endif
}


const char *
svn_sysinfo__release_name(apr_pool_t *pool)
{
#ifdef WIN32
  return win32_release_name(pool);
#else
  return NULL;
#endif
}


const char *
svn_sysinfo__loaded_libs(apr_pool_t *pool)
{
#ifdef WIN32
  return win32_shared_libs(pool);
#else
  return NULL;
#endif
}


#if HAVE_UNAME
static const char*
canonical_host_from_uname(apr_pool_t *pool)
{
  const char *machine = "unknown";
  const char *vendor = "unknown";
  const char *sysname = "unknown";
  const char *sysver = "";
  struct utsname info;

  if (0 <= uname(&info))
    {
      svn_error_t *err;
      const char *tmp;

      err = svn_utf_cstring_to_utf8(&tmp, info.machine, pool);
      if (err)
        svn_error_clear(err);
      else
        machine = tmp;

      err = svn_utf_cstring_to_utf8(&tmp, info.sysname, pool);
      if (err)
        svn_error_clear(err);
      else
        {
          char *lwr = apr_pstrdup(pool, tmp);
          char *it = lwr;
          while (*it)
            {
              if (svn_ctype_isupper(*it))
                *it = apr_tolower(*it);
              ++it;
            }
          sysname = lwr;
        }

      if (0 == strcmp(sysname, "darwin"))
        vendor = "apple";

      err = svn_utf_cstring_to_utf8(&tmp, info.release, pool);
      if (err)
        svn_error_clear(err);
      else
        sysver = tmp;
    }

  return apr_psprintf(pool, "%s-%s-%s%s", machine, vendor, sysname, sysver);
}
#endif  /* HAVE_UNAME */


#ifdef WIN32
typedef DWORD (WINAPI *FNGETNATIVESYSTEMINFO)(LPSYSTEM_INFO);
typedef BOOL (WINAPI *FNENUMPROCESSMODULES) (HANDLE, HMODULE, DWORD, LPDWORD);

/* Get sysstem and version info, and try to tell the difference
   between the native system type and the runtime environment of the
   current process. Populate results in SYSINFO, LOCAL_SYSINFO
   (optional) and OSINFO. */
static BOOL
system_info(SYSTEM_INFO *sysinfo,
            SYSTEM_INFO *local_sysinfo,
            OSVERSIONINFOEXW *osinfo)
{
  FNGETNATIVESYSTEMINFO GetNativeSystemInfo_ = (FNGETNATIVESYSTEMINFO)
    GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetNativeSystemInfo");

  ZeroMemory(sysinfo, sizeof *sysinfo);
  if (local_sysinfo)
    {
      ZeroMemory(local_sysinfo, sizeof *local_sysinfo);
      GetSystemInfo(local_sysinfo);
      if (GetNativeSystemInfo_)
        GetNativeSystemInfo_(sysinfo);
      else
        memcpy(sysinfo, local_sysinfo, sizeof *sysinfo);
    }
  else
    GetSystemInfo(sysinfo);

  ZeroMemory(osinfo, sizeof *osinfo);
  osinfo->dwOSVersionInfoSize = sizeof *osinfo;
  if (!GetVersionExW((LPVOID)osinfo))
    return FALSE;

  return TRUE;
}

/* Map the proccessor type from SYSINFO to a string. */
static const char *
processor_name(SYSTEM_INFO *sysinfo)
{
  switch (sysinfo->wProcessorArchitecture)
    {
    case PROCESSOR_ARCHITECTURE_AMD64:         return "x86_64";
    case PROCESSOR_ARCHITECTURE_IA64:          return "ia64";
    case PROCESSOR_ARCHITECTURE_INTEL:         return "x86";
    case PROCESSOR_ARCHITECTURE_MIPS:          return "mips";
    case PROCESSOR_ARCHITECTURE_ALPHA:         return "alpha32";
    case PROCESSOR_ARCHITECTURE_PPC:           return "powerpc";
    case PROCESSOR_ARCHITECTURE_SHX:           return "shx";
    case PROCESSOR_ARCHITECTURE_ARM:           return "arm";
    case PROCESSOR_ARCHITECTURE_ALPHA64:       return "alpha";
    case PROCESSOR_ARCHITECTURE_MSIL:          return "msil";
    case PROCESSOR_ARCHITECTURE_IA32_ON_WIN64: return "x86_wow64";
    default: return "unknown";
    }
}

/* Return the Windows-specific canonical host name. */
static const char *
win32_canonical_host(apr_pool_t *pool)
{
  SYSTEM_INFO sysinfo;
  SYSTEM_INFO local_sysinfo;
  OSVERSIONINFOEXW osinfo;

  if (system_info(&sysinfo, &local_sysinfo, &osinfo))
    {
      const char *arch = processor_name(&local_sysinfo);
      const char *machine = processor_name(&sysinfo);
      const char *vendor = "microsoft";
      const char *sysname = "windows";
      const char *sysver = apr_psprintf(pool, "%u.%u.%u",
                                        (unsigned int)osinfo.dwMajorVersion,
                                        (unsigned int)osinfo.dwMinorVersion,
                                        (unsigned int)osinfo.dwBuildNumber);

      if (sysinfo.wProcessorArchitecture
          == local_sysinfo.wProcessorArchitecture)
        return apr_psprintf(pool, "%s-%s-%s%s",
                            machine, vendor, sysname, sysver);
      return apr_psprintf(pool, "%s/%s-%s-%s%s",
                          arch, machine, vendor, sysname, sysver);
    }

  return "unknown-microsoft-windows";
}

/* Convert a Unicode string to UTF-8. */
static char *
wcs_to_utf8(const wchar_t *wcs, apr_pool_t *pool)
{
  const int bufsize = WideCharToMultiByte(CP_UTF8, 0, wcs, -1,
                                          NULL, 0, NULL, NULL);
  if (bufsize > 0)
    {
      char *const utf8 = apr_palloc(pool, bufsize + 1);
      WideCharToMultiByte(CP_UTF8, 0, wcs, -1, utf8, bufsize, NULL, NULL);
      return utf8;
    }
  return NULL;
}

/* Query the value called NAME of the registry key HKEY. */
static char *
registry_value(HKEY hkey, wchar_t *name, apr_pool_t *pool)
{
  DWORD size;
  wchar_t *value;

  if (RegQueryValueExW(hkey, name, NULL, NULL, NULL, &size))
    return NULL;

  value = apr_palloc(pool, size + sizeof *value);
  if (RegQueryValueExW(hkey, name, NULL, NULL, (void*)value, &size))
    return NULL;
  value[size / sizeof *value] = 0;
  return wcs_to_utf8(value, pool);
}

/* Try to glean the Windows release name and associated info from the
   registry. Failing that, construct a release name from the version
   info. */
static const char *
win32_release_name(apr_pool_t *pool)
{
  SYSTEM_INFO sysinfo;
  OSVERSIONINFOEXW osinfo;
  HKEY hkcv;

  if (!system_info(&sysinfo, NULL, &osinfo))
    return NULL;

  if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                     L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                     0, KEY_QUERY_VALUE, &hkcv))
    {
      const char *release = registry_value(hkcv, L"ProductName", pool);
      const char *spack = registry_value(hkcv, L"CSDVersion", pool);
      const char *curver = registry_value(hkcv, L"CurrentVersion", pool);
      const char *curtype = registry_value(hkcv, L"CurrentType", pool);
      const char *install = registry_value(hkcv, L"InstallationType", pool);
      const char *curbuild = registry_value(hkcv, L"CurrentBuildNumber", pool);

      if (!spack && *osinfo.szCSDVersion)
        spack = wcs_to_utf8(osinfo.szCSDVersion, pool);

      if (!curbuild)
        curbuild = registry_value(hkcv, L"CurrentBuild", pool);

      if (release || spack || curver || curtype || curbuild)
        {
          const char *bootinfo = "";
          if (curver || install || curtype)
            {
              bootinfo = apr_psprintf(pool, "[%s%s%s%s%s]",
                                      (curver ? curver : ""),
                                      (install ? (curver ? " " : "") : ""),
                                      (install ? install : ""),
                                      (curtype
                                       ? (curver||install ? " " : "")
                                       : ""),
                                      (curtype ? curtype : ""));
            }

          return apr_psprintf(pool, "%s%s%s%s%s%s%s",
                              (release ? release : ""),
                              (spack ? (release ? ", " : "") : ""),
                              (spack ? spack : ""),
                              (curbuild
                               ? (release||spack ? ", build " : "build ")
                               : ""),
                              (curbuild ? curbuild : ""),
                              (bootinfo
                               ? (release||spack||curbuild ? " " : "")
                               : ""),
                              (bootinfo ? bootinfo : ""));
        }
    }

  if (*osinfo.szCSDVersion)
    {
      const char *servicepack = wcs_to_utf8(osinfo.szCSDVersion, pool);

      if (servicepack)
        return apr_psprintf(pool, "Windows NT %u.%u, %s, build %u",
                            (unsigned int)osinfo.dwMajorVersion,
                            (unsigned int)osinfo.dwMinorVersion,
                            servicepack,
                            (unsigned int)osinfo.dwBuildNumber);

      /* Assume wServicePackMajor > 0 if szCSDVersion is not empty */
      if (osinfo.wServicePackMinor)
        return apr_psprintf(pool, "Windows NT %u.%u SP%u.%u, build %u",
                            (unsigned int)osinfo.dwMajorVersion,
                            (unsigned int)osinfo.dwMinorVersion,
                            (unsigned int)osinfo.wServicePackMajor,
                            (unsigned int)osinfo.wServicePackMinor,
                            (unsigned int)osinfo.dwBuildNumber);

      return apr_psprintf(pool, "Windows NT %u.%u SP%u, build %u",
                          (unsigned int)osinfo.dwMajorVersion,
                          (unsigned int)osinfo.dwMinorVersion,
                          (unsigned int)osinfo.wServicePackMajor,
                          (unsigned int)osinfo.dwBuildNumber);
    }

  return apr_psprintf(pool, "Windows NT %u.%u, build %u",
                      (unsigned int)osinfo.dwMajorVersion,
                      (unsigned int)osinfo.dwMinorVersion,
                      (unsigned int)osinfo.dwBuildNumber);
}


/* Get a list of handles of shared libs loaded by the current
   process. Returns a NULL-terminated array alocated from POOL. */
static HMODULE *
enum_loaded_modules(apr_pool_t *pool)
{
  HANDLE current = GetCurrentProcess();
  HMODULE dummy[1];
  HMODULE *handles;
  DWORD size;

  if (!EnumProcessModules(current, dummy, sizeof(dummy), &size))
    return NULL;

  handles = apr_palloc(pool, size + sizeof *handles);
  if (!EnumProcessModules(current, handles, size, &size))
    return NULL;
  handles[size / sizeof *handles] = NULL;
  return handles;
}


/* List the shared libraries loaded by the current process. */
const char *
win32_shared_libs(apr_pool_t *pool)
{
  wchar_t buffer[MAX_PATH + 1];
  HMODULE *handles = enum_loaded_modules(pool);
  char *libinfo = "";
  HMODULE *module;

  for (module = handles; module && *module; ++module)
    {
      const char *filename;
      if (GetModuleFileNameW(*module, buffer, MAX_PATH))
        {
          buffer[MAX_PATH] = 0;
          filename = wcs_to_utf8(buffer, pool);
          if (filename)
            {
              char *truename;
              if (0 == apr_filepath_merge(&truename, "", filename,
                                          APR_FILEPATH_NATIVE
                                          | APR_FILEPATH_TRUENAME,
                                          pool))
                filename = truename;
              libinfo = apr_pstrcat(pool, libinfo, "  - ",
                                    filename, "\n", NULL);
            }
        }
    }

  if (*libinfo)
    return libinfo;
  return NULL;
}
#endif /* WIN32 */
