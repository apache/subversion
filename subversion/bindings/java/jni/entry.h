/*
 * utility functions to handle the java class
 * org.tigris.subversion.lib.Entry
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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

#ifndef SVN_JNI_ENTRY_H
#define SVN_JNI_ENTRY_H

/*** Includes ***/
#include <jni.h>
#include <svn_wc.h>

/*** Functions ***/
jobject
entry__create(JNIEnv *env, jboolean *hasException,
	      svn_wc_entry_t *entry);

#endif

/* 
 * local variables:
 * eval: (load-file "../../../svn-dev.el")
 * end: 
 */
