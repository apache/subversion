/*
 * utility functions to handle the class
 * org.tigris.subversion.lib.Item
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

#ifndef SVN_JNI_ITEM_H
#define SVN_JNI_ITEM_H

/* includes */
#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* functions */
jobject
item__create(JNIEnv *env, jobject jpath, jobject jstatus, 
	     jboolean *hasException);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif

/* 
 * local variables:
 * eval: (load-file "../../../../tools/dev/svn-dev.el")
 * end: 
 */
