/*
 * config_win.c :  parsing configuration data from the registry
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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

#ifdef SVN_WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

HRESULT
svn_config__win_config_path (char *folder, int system_path)
{
  /* ### Adding CSIDL_FLAG_CREATE here, because those folders really
     must exist.  I'm not too sure about the SHGFP_TYPE_CURRENT
     semancics, though; maybe we should use ..._DEFAULT instead? */

  /* FIXME: The path returned here is *not* in UTF-8, and does *not*
     use / as the path separator.  Have to keep that in mind. */

   const int csidl = (system_path ? CSIDL_COMMON_APPDATA : CSIDL_APPDATA);
   return SHGetFolderPathA (NULL, csidl | CSIDL_FLAG_CREATE, NULL,
                            SHGFP_TYPE_CURRENT, folder);
}



#include "config_impl.h"

/* ### These constants are insanely large, but (a) we want to avoid
   reallocating strings if possible, and (b) the realloc logic might
   not actually work -- you never know with Win32 ... */
#define SVN_REG_DEFAULT_NAME_SIZE  2048
#define SVN_REG_DEFAULT_VALUE_SIZE 8192

static svn_error_t *
parse_section (svn_config_t *cfg, HKEY hkey, const char *section,
               svn_stringbuf_t *option, svn_stringbuf_t *value)
{
  DWORD option_len, type, index;
  LONG err;

  /* Start with a reasonable size for the buffers. */
  svn_stringbuf_ensure (option, SVN_REG_DEFAULT_NAME_SIZE);
  svn_stringbuf_ensure (value, SVN_REG_DEFAULT_VALUE_SIZE);
  for (index = 0; ; ++index)
    {
      option_len = option->blocksize;
      err = RegEnumValue (hkey, index, option->data, &option_len,
                          NULL, &type, NULL, NULL);
      if (err == ERROR_NO_MORE_ITEMS)
          break;
      if (err == ERROR_INSUFFICIENT_BUFFER)
        {
          svn_stringbuf_ensure (option, option_len);
          err = RegEnumValue (hkey, index, option->data, &option_len,
                              NULL, &type, NULL, NULL);
        }
      if (err != ERROR_SUCCESS)
        return svn_error_create (SVN_ERR_MALFORMED_FILE,
                                 APR_FROM_OS_ERROR(err), NULL,
                                 "Can't enumerate registry values");

      /* Ignore option names that start with '#', see
         http://subversion.tigris.org/issues/show_bug.cgi?id=671 */
      if (type == REG_SZ && option->data[0] != '#')
        {
          DWORD value_len = value->blocksize;
          err = RegQueryValueEx (hkey, option->data, NULL, NULL,
                                 value->data, &value_len);
          if (err == ERROR_MORE_DATA)
            {
              svn_stringbuf_ensure (value, value_len);
              err = RegQueryValueEx (hkey, option->data, NULL, NULL,
                                     value->data, &value_len);
            }
          if (err != ERROR_SUCCESS)
            return svn_error_create (SVN_ERR_MALFORMED_FILE,
                                     APR_FROM_OS_ERROR(err), NULL,
                                     "Can't read registry value data");

          svn_config_set (cfg, section, option->data, value->data);
        }
    }

  return SVN_NO_ERROR;
}



/*** Exported interface. ***/

svn_error_t *
svn_config__parse_registry (svn_config_t *cfg, const char *file,
                            svn_boolean_t must_exist)
{
  apr_pool_t *subpool;
  svn_stringbuf_t *section, *option, *value;
  svn_error_t *svn_err = SVN_NO_ERROR;
  HKEY base_hkey, hkey;
  DWORD index;
  LONG err;

  if (0 == strncmp (file, SVN_REGISTRY_HKLM, SVN_REGISTRY_HKLM_LEN))
    {
      base_hkey = HKEY_LOCAL_MACHINE;
      file += SVN_REGISTRY_HKLM_LEN;
    }
  else if (0 == strncmp (file, SVN_REGISTRY_HKCU, SVN_REGISTRY_HKCU_LEN))
    {
      base_hkey = HKEY_CURRENT_USER;
      file += SVN_REGISTRY_HKCU_LEN;
    }
  else
    {
      return svn_error_createf (SVN_ERR_BAD_FILENAME,
                                0, NULL,
                                "Unrecognised registry path \"%s\"", file);
    }

  err = RegOpenKeyEx (base_hkey, file, 0,
                      KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE,
                      &hkey);
  if (err != ERROR_SUCCESS)
    {
      const int is_enoent = APR_STATUS_IS_ENOENT(APR_FROM_OS_ERROR(err));
      if (!is_enoent)
        return svn_error_createf (SVN_ERR_BAD_FILENAME,
                                  errno, NULL,
                                  "Can't open registry key \"%s\"", file);
      else if (must_exist && is_enoent)
        return svn_error_createf (SVN_ERR_BAD_FILENAME,
                                  errno, NULL,
                                  "Can't find registry key \"%s\"", file);
      else
        return SVN_NO_ERROR;
    }


  subpool = svn_pool_create (cfg->pool);
  section = svn_stringbuf_create ("", subpool);
  option = svn_stringbuf_create ("", subpool);
  value = svn_stringbuf_create ("", subpool);

  /* The top-level values belong to the [DEFAULT] section */
  svn_err = parse_section (cfg, hkey, "DEFAULT", option, value);
  if (svn_err)
    goto cleanup;

  /* Now enumerate the rest of the keys. */
  svn_stringbuf_ensure (section, SVN_REG_DEFAULT_NAME_SIZE);
  for (index = 0; ; ++index)
    {
      DWORD section_len = section->blocksize;
      FILETIME last_write_time;
      HKEY sub_hkey;

      err = RegEnumKeyEx (hkey, index, section->data, &section_len,
                          NULL, NULL, NULL, &last_write_time);
      if (err == ERROR_NO_MORE_ITEMS)
          break;
      if (err == ERROR_MORE_DATA)
        {
          svn_stringbuf_ensure (section, section_len);
          err = RegEnumKeyEx (hkey, index, section->data, &section_len,
                              NULL, NULL, NULL, &last_write_time);
        }
      if (err != ERROR_SUCCESS)
        {
          svn_err =  svn_error_create (SVN_ERR_MALFORMED_FILE,
                                       APR_FROM_OS_ERROR(err),
                                       NULL,
                                       "Can't enumerate registry keys");
          goto cleanup;
        }

      err = RegOpenKeyEx (hkey, section->data, 0,
                          KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE,
                          &sub_hkey);
      if (err != ERROR_SUCCESS)
        {
          svn_err =  svn_error_create (SVN_ERR_MALFORMED_FILE,
                                       APR_FROM_OS_ERROR(err),
                                       NULL,
                                       "Can't open existing subkey");
          goto cleanup;
        }

      svn_err = parse_section (cfg, sub_hkey, section->data, option, value);
      RegCloseKey (sub_hkey);
      if (svn_err)
        goto cleanup;
    }

 cleanup:
  RegCloseKey (hkey);
  svn_pool_destroy (subpool);
  return svn_err;
}

#endif /* SVN_WIN32 */
