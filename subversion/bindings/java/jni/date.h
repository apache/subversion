/*
 * utility functions to handle the java class
 * java.util.Date
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

#ifndef SVN_JNI_DATE_H
#define SVN_JNI_DATE_H

/* includes */
#include <jni.h>
#include <apr_time.h>

/* functions */

/**
 * creates a java.util.Date instance. Do not pass an apr_time_t
 * value because this is a different format (number of MICROSECONDS)
 * since 1970-01-01 00:00:00 GMT
 *
 * @see date__create_from_apr_time_t
 *
 * @param env JNI Environment
 * @param hasException
 * @param time intial value for the new instance - milliseconds
 *        since 1970-01-01 00:00:00 GMT
 * @return java.util.Date
 */
jobject 
date__create(JNIEnv *env, jboolean *hasException, jlong time);

/**
 * creates a java.util.Date instance out of a apr_time_t values
 *
 * @param env JNI Environment
 * @param hasException
 * @param time - microseconds since 1970-01-01 00:00:00 GMT
 * @return java.util.Date
 */
jobject
date__create_from_apr_time_t(JNIEnv *env, jboolean *hasException, 
                             apr_time_t time);

#endif
/* 
 * local variables:
 * eval: (load-file "../../../../tools/dev/svn-dev.el")
 * end: 
 */



