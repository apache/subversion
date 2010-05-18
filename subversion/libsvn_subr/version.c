/*
 * version.c:  library version number and utilities
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



#include "svn_error.h"
#include "svn_version.h"

#include "svn_private_config.h"

const svn_version_t *
svn_subr_version(void)
{
  SVN_VERSION_BODY;
}


svn_boolean_t svn_ver_compatible(const svn_version_t *my_version,
                                 const svn_version_t *lib_version)
{
  if (lib_version->tag[0] != '\0')
    /* Development library; require exact match. */
    return svn_ver_equal(my_version, lib_version);
  else if (my_version->tag[0] != '\0')
    /* Development client; must be newer than the library
       and have the same major and minor version. */
    return (my_version->major == lib_version->major
            && my_version->minor == lib_version->minor
            && my_version->patch > lib_version->patch);
  else
    /* General compatibility rules for released versions. */
    return (my_version->major == lib_version->major
            && my_version->minor <= lib_version->minor);
}


svn_boolean_t svn_ver_equal(const svn_version_t *my_version,
                            const svn_version_t *lib_version)
{
  return (my_version->major == lib_version->major
          && my_version->minor == lib_version->minor
          && my_version->patch == lib_version->patch
          && 0 == strcmp(my_version->tag, lib_version->tag));
}


svn_error_t *
svn_ver_check_list(const svn_version_t *my_version,
                   const svn_version_checklist_t *checklist)
{
  svn_error_t *err = SVN_NO_ERROR;
  int i;

  for (i = 0; checklist[i].label != NULL; ++i)
    {
      const svn_version_t *lib_version = checklist[i].version_query();
      if (!svn_ver_compatible(my_version, lib_version))
        err = svn_error_createf(SVN_ERR_VERSION_MISMATCH, err,
                                _("Version mismatch in '%s':"
                                  " found %d.%d.%d%s,"
                                  " expected %d.%d.%d%s"),
                                checklist[i].label,
                                lib_version->major, lib_version->minor,
                                lib_version->patch, lib_version->tag,
                                my_version->major, my_version->minor,
                                my_version->patch, my_version->tag);
    }

  return err;
}
