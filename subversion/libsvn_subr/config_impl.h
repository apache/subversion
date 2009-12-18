/*
 * config_impl.h :  private header for the config file implementation.
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



#ifndef SVN_LIBSVN_SUBR_CONFIG_IMPL_H
#define SVN_LIBSVN_SUBR_CONFIG_IMPL_H

#define APR_WANT_STDIO
#include <apr_want.h>

#include <apr_hash.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_config.h"
#include "svn_private_config.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* The configuration data. This is a superhash of sections and options. */
struct svn_config_t
{
  /* Table of cfg_section_t's. */
  apr_hash_t *sections;

  /* Pool for hash tables, table entries and unexpanded values */
  apr_pool_t *pool;

  /* Pool for expanded values -- this is separate, so that we can
     clear it when modifying the config data. */
  apr_pool_t *x_pool;

  /* Indicates that some values in the configuration have been expanded. */
  svn_boolean_t x_values;

  /* Temporary string used for lookups.  (Using a stringbuf so that
     frequent resetting is efficient.) */
  svn_stringbuf_t *tmp_key;

  /* Temporary value used for expanded default values in svn_config_get.
     (Using a stringbuf so that frequent resetting is efficient.) */
  svn_stringbuf_t *tmp_value;
};


/* Read sections and options from a file. */
svn_error_t *svn_config__parse_file(svn_config_t *cfg,
                                    const char *file,
                                    svn_boolean_t must_exist,
                                    apr_pool_t *pool);

/* The name of the magic [DEFAULT] section. */
#define SVN_CONFIG__DEFAULT_SECTION "DEFAULT"


#ifdef WIN32
/* Get the common or user-specific AppData folder */
svn_error_t *svn_config__win_config_path(const char **folder,
                                         int system_path,
                                         apr_pool_t *pool);

/* Read sections and options from the Windows Registry. */
svn_error_t *svn_config__parse_registry(svn_config_t *cfg,
                                        const char *file,
                                        svn_boolean_t must_exist,
                                        apr_pool_t *pool);

/* ### It's unclear to me whether this registry stuff should get the
   double underscore or not, and if so, where the extra underscore
   would go.  Thoughts?  -kff */
#  define SVN_REGISTRY_PREFIX "REGISTRY:"
#  define SVN_REGISTRY_PREFIX_LEN ((sizeof(SVN_REGISTRY_PREFIX)) - 1)
#  define SVN_REGISTRY_HKLM "HKLM\\"
#  define SVN_REGISTRY_HKLM_LEN ((sizeof(SVN_REGISTRY_HKLM)) - 1)
#  define SVN_REGISTRY_HKCU "HKCU\\"
#  define SVN_REGISTRY_HKCU_LEN ((sizeof(SVN_REGISTRY_HKCU)) - 1)
#  define SVN_REGISTRY_PATH "Software\\Tigris.org\\Subversion\\"
#  define SVN_REGISTRY_PATH_LEN ((sizeof(SVN_REGISTRY_PATH)) - 1)
#  define SVN_REGISTRY_SYS_CONFIG_PATH \
                               SVN_REGISTRY_PREFIX     \
                               SVN_REGISTRY_HKLM       \
                               SVN_REGISTRY_PATH
#  define SVN_REGISTRY_USR_CONFIG_PATH \
                               SVN_REGISTRY_PREFIX     \
                               SVN_REGISTRY_HKCU       \
                               SVN_REGISTRY_PATH
#endif /* WIN32 */

/* System-wide and configuration subdirectory names.
   NOTE: Don't use these directly; call svn_config__sys_config_path()
   or svn_config_get_user_config_path() instead. */
#ifdef WIN32
#  define SVN_CONFIG__SUBDIRECTORY    "Subversion"
#else  /* ! WIN32 */
#  define SVN_CONFIG__SYS_DIRECTORY   "/etc/subversion"
#  define SVN_CONFIG__USR_DIRECTORY   ".subversion"
#endif /* WIN32 */

/* The description/instructions file in the config directory. */
#define SVN_CONFIG__USR_README_FILE    "README.txt"

/* The name of the main authentication subdir in the config directory */
#define SVN_CONFIG__AUTH_SUBDIR        "auth"

/* Set *PATH_P to the path to config file FNAME in the system
   configuration area, allocated in POOL.  If FNAME is NULL, set
   *PATH_P to the directory name of the system config area, either
   allocated in POOL or a static constant string.

   If the system configuration area cannot be located (possible under
   Win32), set *PATH_P to NULL regardless of FNAME.  */
svn_error_t *
svn_config__sys_config_path(const char **path_p,
                            const char *fname,
                            apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_SUBR_CONFIG_IMPL_H */
