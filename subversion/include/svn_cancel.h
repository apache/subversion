/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
 * @endcopyright
 *
 * @file svn_cancel.h
 * @brief Support for cancelation of running subversion functions.
 */

#ifndef SVN_CANCEL_H
#define SVN_CANCEL_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** A user defined callback that subversion will call with a user defined 
 * baton to see if the current operation should be continued.  If the operation 
 * should continue, the function should return @c SVN_NO_ERROR, if not, it 
 * should return @c SVN_ERR_CANCELLED.
 */
typedef svn_error_t *(*svn_cancel_func_t) (void *cancel_baton);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CANCEL_H */
