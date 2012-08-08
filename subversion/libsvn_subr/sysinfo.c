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
#include <windows.h>
#endif

#define APR_WANT_STRFUNC
#include <apr_want.h>

#include <apr_lib.h>
#include <apr_pools.h>

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
  return NULL;
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
typedef BOOL (WINAPI *FNGETPRODUCTINFO)(DWORD, DWORD, DWORD, DWORD, PDWORD);

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

static char *
default_release_name(OSVERSIONINFOEXW *osinfo, apr_pool_t *pool)
{
  return apr_psprintf(pool, "Windows v%u.%u",
                      (unsigned int)osinfo->dwMajorVersion,
                      (unsigned int)osinfo->dwMinorVersion);
}

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

static const char *
win32_release_name(apr_pool_t *pool)
{
  SYSTEM_INFO sysinfo;
  OSVERSIONINFOEXW osinfo;
  char *relname = NULL;

  if (!system_info(&sysinfo, NULL, &osinfo))
    return NULL;

  if (6 == osinfo.dwMajorVersion)
    {
      FNGETPRODUCTINFO GetProductInfo_ = (FNGETPRODUCTINFO)
        GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetProductInfo");
      DWORD product_type;

      if (osinfo.wProductType == VER_NT_WORKSTATION)
        switch (osinfo.dwMinorVersion)
          {
          case 2: relname = "Windows 8"; break;
          case 1: relname = "Windows 7"; break;
          case 0: relname = "Windows Vista"; break;
          }
      else
        switch (osinfo.dwMinorVersion)
          {
          case 2: relname = "Windows Server 2012"; break;
          case 1: relname = "Windows Server 2008 R2"; break;
          case 0: relname = "Windows Server 2008"; break;
          }
      if (!relname)
        relname = default_release_name(&osinfo, pool);

      GetProductInfo_(osinfo.dwMajorVersion, osinfo.dwMinorVersion,
                      0, 0, &product_type);
      switch (product_type)
        {
        case PRODUCT_ULTIMATE:
          relname = apr_pstrcat(pool, relname, " Ultimate Edition", NULL);
          break;
        case PRODUCT_PROFESSIONAL:
          relname = apr_pstrcat(pool, relname, " Professional", NULL);
          break;
        case PRODUCT_HOME_PREMIUM:
          relname = apr_pstrcat(pool, relname, " Home Premium Edition", NULL);
          break;
        case PRODUCT_HOME_BASIC:
          relname = apr_pstrcat(pool, relname, " Home Basic Edition", NULL);
          break;
        case PRODUCT_ENTERPRISE:
          relname = apr_pstrcat(pool, relname, " Enterprise Edition", NULL);
          break;
        case PRODUCT_BUSINESS:
          relname = apr_pstrcat(pool, relname, " Business Edition", NULL);
          break;
        case PRODUCT_STARTER:
          relname = apr_pstrcat(pool, relname, " Starter Edition", NULL);
          break;
        case PRODUCT_CLUSTER_SERVER:
          relname = apr_pstrcat(pool, relname,
                                " Cluster Server Edition", NULL);
          break;
        case PRODUCT_DATACENTER_SERVER:
          relname = apr_pstrcat(pool, relname, " Datacenter Edition", NULL);
          break;
        case PRODUCT_DATACENTER_SERVER_CORE:
          relname = apr_pstrcat(pool, relname,
                                " Datacenter Edition (core installation)",
                                NULL);
          break;
        case PRODUCT_ENTERPRISE_SERVER:
          relname = apr_pstrcat(pool, relname, " Enterprise Edition", NULL);
          break;
        case PRODUCT_ENTERPRISE_SERVER_CORE:
          relname = apr_pstrcat(pool, relname,
                                " Enterprise Edition (core installation)",
                                NULL);
          break;
        case PRODUCT_ENTERPRISE_SERVER_IA64:
          relname = apr_pstrcat(pool, relname,
                                " Enterprise Edition for Itanium", NULL);
          break;
        case PRODUCT_SMALLBUSINESS_SERVER:
          relname = apr_pstrcat(pool, relname,
                                " Small Business Server Edition", NULL);
          break;
        case PRODUCT_SMALLBUSINESS_SERVER_PREMIUM:
          relname = apr_pstrcat(pool, relname,
                                " Small Business Server Premium Edition",
                                NULL);
          break;
        case PRODUCT_STANDARD_SERVER:
          relname = apr_pstrcat(pool, relname, " Standard Edition", NULL);
          break;
        case PRODUCT_STANDARD_SERVER_CORE:
          relname = apr_pstrcat(pool, relname,
                                " Standard Edition (core installation)",
                                NULL);
          break;
        case PRODUCT_WEB_SERVER:
          relname = apr_pstrcat(pool, relname, " Web Server Edition", NULL);
          break;
        }
    }
  else if (5 == osinfo.dwMajorVersion)
    {
      switch (osinfo.dwMinorVersion)
        {
        case 2:
          if (GetSystemMetrics(SM_SERVERR2))
            relname = "Windows Server 2003 R2";
          else if (osinfo.wSuiteMask & VER_SUITE_STORAGE_SERVER)
            relname = "Windows Storage Server 2003";
          else if (osinfo.wSuiteMask & VER_SUITE_WH_SERVER)
            relname = "Windows Home Server";
          else if (osinfo.wProductType == VER_NT_WORKSTATION
                   && (sysinfo.wProcessorArchitecture
                       == PROCESSOR_ARCHITECTURE_AMD64))
            relname = "Windows XP Professional x64 Edition";
          else
            relname = "Windows Server 2003";

          if (osinfo.wProductType != VER_NT_WORKSTATION)
            switch (sysinfo.wProcessorArchitecture)
              {
              case PROCESSOR_ARCHITECTURE_IA64:
                if (osinfo.wSuiteMask & VER_SUITE_DATACENTER)
                  relname = apr_pstrcat(pool, relname,
                                        " Datacenter Edition for Itanium",
                                        NULL);
                else if (osinfo.wSuiteMask & VER_SUITE_ENTERPRISE)
                  relname = apr_pstrcat(pool, relname,
                                        " Enterprise Edition for Itanium",
                                        NULL);
                break;

              case PROCESSOR_ARCHITECTURE_AMD64:
                if (osinfo.wSuiteMask & VER_SUITE_DATACENTER)
                  relname = apr_pstrcat(pool, relname,
                                        " Datacenter x64 Edition", NULL);
                else if (osinfo.wSuiteMask & VER_SUITE_ENTERPRISE)
                  relname = apr_pstrcat(pool, relname,
                                        " Enterprise x64 Edition", NULL);
                else
                  relname = apr_pstrcat(pool, relname,
                                        " Standard x64 Edition", NULL);
                break;

              default:
                if (osinfo.wSuiteMask & VER_SUITE_COMPUTE_SERVER)
                  relname = apr_pstrcat(pool, relname,
                                        " Compute Cluster Edition", NULL);
                else if (osinfo.wSuiteMask & VER_SUITE_DATACENTER)
                  relname = apr_pstrcat(pool, relname,
                                        " Datacenter Edition", NULL);
                else if (osinfo.wSuiteMask & VER_SUITE_ENTERPRISE)
                  relname = apr_pstrcat(pool, relname,
                                        " Enterprise Edition", NULL);
                else if (osinfo.wSuiteMask & VER_SUITE_BLADE)
                  relname = apr_pstrcat(pool, relname, " Web Edition", NULL);
                else
                  relname = apr_pstrcat(pool, relname,
                                        " Standard Edition", NULL);
              }
          break;

        case 1:
          if (osinfo.wSuiteMask & VER_SUITE_PERSONAL)
            relname = "Windows XP Home";
          else
            relname = "Windows XP Professional";
          break;

        case 0:
          if (osinfo.wProductType == VER_NT_WORKSTATION)
            relname = "Windows 2000 Professional";
          else
            {
              if (osinfo.wSuiteMask & VER_SUITE_DATACENTER)
                relname = "Windows 2000 Datacenter Server";
              else if (osinfo.wSuiteMask & VER_SUITE_ENTERPRISE)
                relname = "Windows 2000 Advanced Server";
              else
                relname = "Windows 2000 Server";
            }
          break;

        default:
          relname = default_release_name(&osinfo, pool);
        }
    }
  else if (5 > osinfo.dwMajorVersion)
    {
      relname = apr_psprintf(pool, "Windows NT %d.%d%s",
                             (unsigned int)osinfo.dwMajorVersion,
                             (unsigned int)osinfo.dwMinorVersion,
                             (osinfo.wProductType != VER_NT_WORKSTATION
                              ? " Server" : ""));
    }
  else
    {
      relname = default_release_name(&osinfo, pool);
    }

  if (*osinfo.szCSDVersion)
    {
      const int bufsize = WideCharToMultiByte(CP_UTF8, 0,
                                              osinfo.szCSDVersion, -1,
                                              NULL, 0, NULL, NULL);
      if (bufsize > 0)
        {
          char *const servicepack = apr_palloc(pool, bufsize + 1);
          WideCharToMultiByte(CP_UTF8, 0,
                              osinfo.szCSDVersion, -1,
                              servicepack, bufsize,
                              NULL, NULL);
          relname = apr_psprintf(pool, "%s, %s, build %d",
                                 relname, servicepack,
                                 (unsigned int)osinfo.dwBuildNumber);
        }
      /* Assume wServicePackMajor > 0 if szCSDVersion is not empty */
      else if (osinfo.wServicePackMinor)
        relname = apr_psprintf(pool, "%s SP%d.%d, build %d", relname,
                               (unsigned int)osinfo.wServicePackMajor,
                               (unsigned int)osinfo.wServicePackMinor,
                               (unsigned int)osinfo.dwBuildNumber);
      else
        relname = apr_psprintf(pool, "%s SP%d, build %d", relname,
                               (unsigned int)osinfo.wServicePackMajor,
                               (unsigned int)osinfo.dwBuildNumber);
    }
  else
    {
      relname = apr_psprintf(pool, "%s, build %d", relname,
                             (unsigned int)osinfo.dwBuildNumber);
    }

  return relname;
}
#endif /* WIN32 */
