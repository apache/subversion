/*
 * config_win.c :  parsing configuration data from the registry
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */



#include "svn_private_config.h"

#ifdef WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

#include <apr_file_info.h>
/* FIXME: We're using an internal APR header here, which means we
   have to build Subversion with APR sources. This being Win32-only,
   that should be fine for now, but a better solution must be found in
   combination with issue #850. */
#include <arch/win32/apr_arch_utf8.h>

#include "svn_error.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_utf.h"

svn_error_t *
svn_config__win_config_path(const char **folder, int system_path,
                            apr_pool_t *pool)
{
  /* ### Adding CSIDL_FLAG_CREATE here, because those folders really
     must exist.  I'm not too sure about the SHGFP_TYPE_CURRENT
     semancics, though; maybe we should use ..._DEFAULT instead? */
  const int csidl = ((system_path ? CSIDL_COMMON_APPDATA : CSIDL_APPDATA)
                     | CSIDL_FLAG_CREATE);

  int style;
  apr_status_t apr_err = apr_filepath_encoding(&style, pool);

  if (apr_err)
    return svn_error_wrap_apr(apr_err,
                              "Can't determine the native path encoding");

  if (style == APR_FILEPATH_ENCODING_UTF8)
    {
      WCHAR folder_ucs2[MAX_PATH];
      apr_size_t inwords, outbytes, outlength;
      char *folder_utf8;

      if (S_OK != SHGetFolderPathW(NULL, csidl, NULL, SHGFP_TYPE_CURRENT,
                                   folder_ucs2))
        goto no_folder_path;

      /* ### When mapping from UCS-2 to UTF-8, we need at most 3 bytes
             per wide char, plus extra space for the nul terminator. */
      inwords = lstrlenW(folder_ucs2);
      outbytes = outlength = 3 * (inwords + 1);
      folder_utf8 = apr_palloc(pool, outlength);

      apr_err = apr_conv_ucs2_to_utf8(folder_ucs2, &inwords,
                                      folder_utf8, &outbytes);
      if (!apr_err && (inwords > 0 || outbytes == 0))
        apr_err = APR_INCOMPLETE;
      if (apr_err)
        return svn_error_wrap_apr(apr_err,
                                  "Can't convert config path to UTF-8");

      /* Note that apr_conv_ucs2_to_utf8 does _not_ terminate the
         outgoing buffer. */
      folder_utf8[outlength - outbytes] = '\0';
      *folder = folder_utf8;
    }
  else if (style == APR_FILEPATH_ENCODING_LOCALE)
    {
      char folder_ansi[MAX_PATH];
      if (S_OK != SHGetFolderPathA(NULL, csidl, NULL, SHGFP_TYPE_CURRENT,
                                   folder_ansi))
        goto no_folder_path;
      SVN_ERR(svn_utf_cstring_to_utf8(folder, folder_ansi, pool));
    }
  else
    {
      /* There is no third option on Windows; we should never get here. */
      return svn_error_createf(APR_EINVAL, NULL,
                               "Unknown native path encoding (%d)", style);
    }

  *folder = svn_path_internal_style(*folder, pool);
  return SVN_NO_ERROR;

 no_folder_path:
  return svn_error_create(SVN_ERR_BAD_FILENAME, NULL,
                          (system_path
                           ? "Can't determine the system config path"
                           : "Can't determine the user's config path"));
}

/* Convert UTF8, a UTF-8 encoded string, to UCS2, a UCS-2 encoded
   string, using POOL for temporary allocations. */
static svn_error_t *
utf8_to_ucs2(WCHAR **ucs2, const char *utf8, apr_pool_t *pool)
{
  apr_size_t inbytes, outwords, outlength;
  apr_status_t apr_err;

  inbytes = lstrlenA(utf8);
  outwords = outlength = inbytes + 1; /* Include terminating null. */
  *ucs2 = apr_palloc(pool, outwords * sizeof(WCHAR));
  apr_err = apr_conv_utf8_to_ucs2(utf8, &inbytes, *ucs2, &outwords);

  if (!apr_err && (inbytes > 0 || outwords == 0))
    apr_err = APR_INCOMPLETE;
  if (apr_err)
    return svn_error_wrap_apr(apr_err, "Can't convert config path to UCS-2");

  /* Note that apr_conv_utf8_to_ucs2 does _not_ terminate the
     outgoing buffer. */
  (*ucs2)[outlength - outwords] = L'\0';
  return SVN_NO_ERROR;
}



#include "config_impl.h"

svn_error_t *
svn_config__open_file(FILE **pfile,
                      const char *filename,
                      const char *mode,
                      apr_pool_t *pool)
{
  int style;
  apr_status_t apr_err = apr_filepath_encoding(&style, pool);

  if (apr_err)
    return svn_error_wrap_apr(apr_err,
                              "Can't determine the native path encoding");

  if (style == APR_FILEPATH_ENCODING_UTF8)
    {
      WCHAR *filename_ucs2;
      WCHAR *mode_ucs2;

      SVN_ERR(utf8_to_ucs2(&filename_ucs2, filename, pool));
      SVN_ERR(utf8_to_ucs2(&mode_ucs2, mode, pool));
      *pfile = _wfopen(filename_ucs2, mode_ucs2);
    }
  else if (style == APR_FILEPATH_ENCODING_LOCALE)
    {
      const char *filename_native;
      SVN_ERR(svn_utf_cstring_from_utf8(&filename_native, filename, pool));
      *pfile = fopen(filename_native, mode);
    }
  else
    {
      /* There is no third option on Windows; we should never get here. */
      return svn_error_createf(APR_EINVAL, NULL,
                               "Unknown native path encoding (%d)", style);
    }

  return SVN_NO_ERROR;
}

/* ### These constants are insanely large, but (a) we want to avoid
   reallocating strings if possible, and (b) the realloc logic might
   not actually work -- you never know with Win32 ... */
#define SVN_REG_DEFAULT_NAME_SIZE  2048
#define SVN_REG_DEFAULT_VALUE_SIZE 8192

static svn_error_t *
parse_section(svn_config_t *cfg, HKEY hkey, const char *section,
              svn_stringbuf_t *option, svn_stringbuf_t *value)
{
  DWORD option_len, type, index;
  LONG err;

  /* Start with a reasonable size for the buffers. */
  svn_stringbuf_ensure(option, SVN_REG_DEFAULT_NAME_SIZE);
  svn_stringbuf_ensure(value, SVN_REG_DEFAULT_VALUE_SIZE);
  for (index = 0; ; ++index)
    {
      option_len = option->blocksize;
      err = RegEnumValue(hkey, index, option->data, &option_len,
                         NULL, &type, NULL, NULL);
      if (err == ERROR_NO_MORE_ITEMS)
          break;
      if (err == ERROR_INSUFFICIENT_BUFFER)
        {
          svn_stringbuf_ensure(option, option_len);
          err = RegEnumValue(hkey, index, option->data, &option_len,
                             NULL, &type, NULL, NULL);
        }
      if (err != ERROR_SUCCESS)
        return svn_error_create(SVN_ERR_MALFORMED_FILE, NULL,
                                "Can't enumerate registry values");

      /* Ignore option names that start with '#', see
         http://subversion.tigris.org/issues/show_bug.cgi?id=671 */
      if (type == REG_SZ && option->data[0] != '#')
        {
          DWORD value_len = value->blocksize;
          err = RegQueryValueEx(hkey, option->data, NULL, NULL,
                                value->data, &value_len);
          if (err == ERROR_MORE_DATA)
            {
              svn_stringbuf_ensure(value, value_len);
              err = RegQueryValueEx(hkey, option->data, NULL, NULL,
                                    value->data, &value_len);
            }
          if (err != ERROR_SUCCESS)
            return svn_error_create(SVN_ERR_MALFORMED_FILE, NULL,
                                    "Can't read registry value data");

          svn_config_set(cfg, section, option->data, value->data);
        }
    }

  return SVN_NO_ERROR;
}



/*** Exported interface. ***/

svn_error_t *
svn_config__parse_registry(svn_config_t *cfg, const char *file,
                           svn_boolean_t must_exist, apr_pool_t *pool)
{
  apr_pool_t *subpool;
  svn_stringbuf_t *section, *option, *value;
  svn_error_t *svn_err = SVN_NO_ERROR;
  HKEY base_hkey, hkey;
  DWORD index;
  LONG err;

  if (0 == strncmp(file, SVN_REGISTRY_HKLM, SVN_REGISTRY_HKLM_LEN))
    {
      base_hkey = HKEY_LOCAL_MACHINE;
      file += SVN_REGISTRY_HKLM_LEN;
    }
  else if (0 == strncmp(file, SVN_REGISTRY_HKCU, SVN_REGISTRY_HKCU_LEN))
    {
      base_hkey = HKEY_CURRENT_USER;
      file += SVN_REGISTRY_HKCU_LEN;
    }
  else
    {
      return svn_error_createf(SVN_ERR_BAD_FILENAME, NULL,
                               "Unrecognised registry path '%s'",
                               svn_path_local_style(file, pool));
    }

  err = RegOpenKeyEx(base_hkey, file, 0,
                     KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE,
                     &hkey);
  if (err != ERROR_SUCCESS)
    {
      const int is_enoent = APR_STATUS_IS_ENOENT(APR_FROM_OS_ERROR(err));
      if (!is_enoent)
        return svn_error_createf(SVN_ERR_BAD_FILENAME, NULL,
                                 "Can't open registry key '%s'",
                                 svn_path_local_style(file, pool));
      else if (must_exist && is_enoent)
        return svn_error_createf(SVN_ERR_BAD_FILENAME, NULL,
                                 "Can't find registry key '%s'",
                                 svn_path_local_style(file, pool));
      else
        return SVN_NO_ERROR;
    }


  subpool = svn_pool_create(pool);
  section = svn_stringbuf_create("", subpool);
  option = svn_stringbuf_create("", subpool);
  value = svn_stringbuf_create("", subpool);

  /* The top-level values belong to the [DEFAULT] section */
  svn_err = parse_section(cfg, hkey, SVN_CONFIG__DEFAULT_SECTION,
                          option, value);
  if (svn_err)
    goto cleanup;

  /* Now enumerate the rest of the keys. */
  svn_stringbuf_ensure(section, SVN_REG_DEFAULT_NAME_SIZE);
  for (index = 0; ; ++index)
    {
      DWORD section_len = section->blocksize;
      FILETIME last_write_time;
      HKEY sub_hkey;

      err = RegEnumKeyEx(hkey, index, section->data, &section_len,
                         NULL, NULL, NULL, &last_write_time);
      if (err == ERROR_NO_MORE_ITEMS)
          break;
      if (err == ERROR_MORE_DATA)
        {
          svn_stringbuf_ensure(section, section_len);
          err = RegEnumKeyEx(hkey, index, section->data, &section_len,
                             NULL, NULL, NULL, &last_write_time);
        }
      if (err != ERROR_SUCCESS)
        {
          svn_err =  svn_error_create(SVN_ERR_MALFORMED_FILE, NULL,
                                      "Can't enumerate registry keys");
          goto cleanup;
        }

      err = RegOpenKeyEx(hkey, section->data, 0,
                         KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE,
                         &sub_hkey);
      if (err != ERROR_SUCCESS)
        {
          svn_err =  svn_error_create(SVN_ERR_MALFORMED_FILE, NULL,
                                      "Can't open existing subkey");
          goto cleanup;
        }

      svn_err = parse_section(cfg, sub_hkey, section->data, option, value);
      RegCloseKey(sub_hkey);
      if (svn_err)
        goto cleanup;
    }

 cleanup:
  RegCloseKey(hkey);
  svn_pool_destroy(subpool);
  return svn_err;
}

#endif /* WIN32 */
