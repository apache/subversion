/* validate.c : internal structure validators
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

#define APR_WANT_STRFUNC
#include "apr_want.h"

#include "validate.h"



/* Validating paths. */

int 
svn_fs__is_single_path_component (const char *name)
{
  /* Can't be empty */
  if (*name == '\0')
    return 0;

  /* Can't be `.' or `..' */
  if (name[0] == '.'
      && (name[1] == '\0'
          || (name[1] == '.' && name[2] == '\0')))
    return 0;

  /* slashes are bad, m'kay... */
  if (strchr(name, '/') != NULL)
    return 0;

  /* it is valid */
  return 1;
}




/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
