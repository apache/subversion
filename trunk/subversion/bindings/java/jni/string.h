/*
 * utility functions to deal with strings:
 * - java strings (java.lang.String)
 * - svn strings (svn_string_t)
 * - c strings (char *)
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

#ifndef SVN_JNI_STRING_H
#define SVN_JNI_STRING_H

/*** Includes ***/
#include <jni.h>
#include <svn_string.h>
#include <apr_pools.h>

/*** Functions ***/

/*
 * conversion to and from java string to the
 * different svn string types
 */
svn_string_t *
string__j_to_svn_string(JNIEnv *env, 
                        jstring jstr, 
                        jboolean *hasException,
                        apr_pool_t *pool);

svn_stringbuf_t *
string__c_to_stringbuf(JNIEnv *env,
                       jstring jstr,
                       jboolean *hasException,
                       apr_pool_t *pool);

jstring
string__svn_string_to_j(JNIEnv *env, 
                        svn_string_t *string, 
                        jboolean *hasException);

jstring
string__svn_stringbuf_to_j(JNIEnv *env,
                           svn_stringbuf_t *stringbuf,
                           jboolean *hasException);

/*
 * conversion to and from c string to java string
 */
jstring
string__c_to_j(JNIEnv *env, 
               const char *string, 
               jboolean *hasException);

svn_stringbuf_t *
string__c_to_stringbuf(JNIEnv *env,
                       jstring jstr,
                       jboolean *hasException,
                       apr_pool_t *pool);
#endif

/* 
 * local variables:
 * eval: (load-file "../../../../tools/dev/svn-dev.el")
 * end: 
 */











