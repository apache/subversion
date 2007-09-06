/*
 * svn_flowcontrol.h: Declarations for flowcontrol helpers.
 *
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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

#ifndef SVN_FLOWCONTROL_H
#define SVN_FLOWCONTROL_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Send processing to LABEL when COND evaluates to true. */
#define MAYBE_GOTO(label, cond) if ((cond)) goto label

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_FLOWCONTROL_H */
