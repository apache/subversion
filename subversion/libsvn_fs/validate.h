/* validate.h : internal interface to structure validators
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

#ifndef SVN_LIBSVN_FS_VALIDATE_H
#define SVN_LIBSVN_FS_VALIDATE_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Validating paths. */

/* Validate that name NAME is a single path component, not a
   slash-separated directory path.  Also, NAME cannot be `.' or `..'
   at this time. */
int svn_fs__is_single_path_component (const char *name);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_VALIDATE_H */


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
