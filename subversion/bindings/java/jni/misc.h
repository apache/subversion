/*
 * svn_jni_misc.c miscelleneous help functions
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

#ifndef SVN_JNI_MISC_H
#define SVN_JNI_MISC_H

/* includes */
#include <jni.h>
#include <svn_client.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* functions */

/*
 * utility function to throw a java exception
 */
void
misc__throw_exception_by_name(JNIEnv *env,
			      const char *name,
			      const char *msg);
svn_client_auth_baton_t *
misc__make_auth_baton(JNIEnv *env, jobject jobj);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif

/* 
 * local variables:
 * eval: (load-file "../../../../tools/dev/svn-dev.el")
 * end: 
 */

