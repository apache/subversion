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



#include "config_impl.h"

#ifdef SVN_WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static svn_error_t *
parse_section (svn_config_t *cfg, HKEY hkey, const char *section)
{
  char option[4096];            /* FIXME: Allocate this! */
  DWORD option_len, type, index;
  LONG err;

  for (index = 0; ; ++index)
    {
      option_len = sizeof (option) / sizeof (option[0]);
      err = RegEnumValue (hkey, index, option, &option_len,
                          NULL, &type, NULL, NULL);
      if (err == ERROR_NO_MORE_ITEMS)
          break;
      if (err == ERROR_MORE_DATA)
        return svn_error_create (-1, /* FIXME: Resize buffer and retry */
                                 APR_FROM_OS_ERROR(err), NULL, cfg->pool,
                                 "Option buffer too small");
      if (err != ERROR_SUCCESS)
        return svn_error_create (SVN_ERR_MALFORMED_FILE,
                                 APR_FROM_OS_ERROR(err), NULL, cfg->pool,
                                 "Can't enumerate registry values");

      if (type == REG_SZ)
        {
          char value[8192];     /* FIXME: Allocate this! */
          DWORD value_len = sizeof (value);
          err = RegQueryValueEx (hkey, option, NULL, NULL,
                                 value, &value_len);
          if (err == ERROR_MORE_DATA)
            return svn_error_create (-1, /* FIXME: Resize buffer and retry */
                                     APR_FROM_OS_ERROR(err), NULL, cfg->pool,
                                     "Value buffer too small");
          else if (err != ERROR_SUCCESS)
            return svn_error_create (SVN_ERR_MALFORMED_FILE,
                                     APR_FROM_OS_ERROR(err), NULL, cfg->pool,
                                     "Can't read registry value data");

          svn_config_set (cfg, section, option, value);
        }
    }

  return SVN_NO_ERROR;
}



/*** Exported interface. ***/

svn_error_t *
svn_config__parse_registry (svn_config_t *cfg, const char *file,
                            svn_boolean_t must_exist)
{
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
                                0, NULL, cfg->pool,
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
                                  errno, NULL, cfg->pool,
                                  "Can't open registry key \"%s\"", file);
      else if (must_exist && is_enoent)
        return svn_error_createf (SVN_ERR_BAD_FILENAME,
                                  errno, NULL, cfg->pool,
                                  "Can't find registry key \"%s\"", file);
      else
        return SVN_NO_ERROR;
    }

  /* The top-level values belong to the [DEFAULTS] section */
  svn_err = parse_section (cfg, hkey, "DEFAULTS");
  if (svn_err)
    goto cleanup;

  /* Now enumerate the rest of the keys. */
  for (index = 0; ; ++index)
    {
      char section[4096];        /* FIXME: Allocate this! */
      DWORD section_len = sizeof (section);
      FILETIME last_write_time;
      HKEY sub_hkey;

      err = RegEnumKeyEx (hkey, index, section, &section_len,
                          NULL, NULL, NULL, &last_write_time);
      if (err == ERROR_NO_MORE_ITEMS)
          break;
      if (err == ERROR_MORE_DATA)
        return svn_error_create (-1, /* FIXME: Resize buffer and retry */
                                 APR_FROM_OS_ERROR(err), NULL, cfg->pool,
                                 "Key buffer too small");
      if (err != ERROR_SUCCESS)
        {
          svn_err =  svn_error_create (SVN_ERR_MALFORMED_FILE,
                                       APR_FROM_OS_ERROR(err),
                                       NULL, cfg->pool,
                                       "Can't enumerate registry keys");
          goto cleanup;
        }

      err = RegOpenKeyEx (hkey, section, 0,
                          KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE,
                          &sub_hkey);
      if (err != ERROR_SUCCESS)
        {
          svn_err =  svn_error_create (SVN_ERR_MALFORMED_FILE,
                                       APR_FROM_OS_ERROR(err),
                                       NULL, cfg->pool,
                                       "Can't open existing subkey");
          goto cleanup;
        }

      svn_err = parse_section (cfg, sub_hkey, section);
      RegCloseKey (sub_hkey);
      if (svn_err)
        goto cleanup;
    }

 cleanup:
  RegCloseKey (hkey);
  return svn_err;
}

#endif /* SVN_WIN32 */



/*
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
