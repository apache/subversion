/*
 * svn_jni_global.h header for all of the java binding utility functions
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

#ifndef SVN_JNI_GLOBAL_H
#define SVN_JNI_GLOBAL_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* svn_jni_hashtable */
jobject
svn_jni_hashtable__create(JNIEnv *env, jboolean *hasException);

void
svn_jni_hashtable__put(JNIEnv *env, jobject hashtable, jobject key,
		       jobject value, jboolean *hasException);

/* svn_jni_status */
jobject
svn_jni_status__create(JNIEnv *env, svn_wc_status_t *status, 
		       jboolean *hasException);

/* svn_jni_item */
jobject
svn_jni_item__create(JNIEnv *env, jobject jpath, jobject jstatus, 
		     jboolean *hasException);

/* svn_jni_string */
svn_string_t *
svn_jni_string__j_to_svn(JNIEnv *env, 
                         jstring jstr, 
                         jboolean *hasException,
                         apr_pool_t *pool);
jstring
svn_jni_string__c_to_j(JNIEnv *env, 
                       char *string, 
                       jboolean *hasException);
jstring
svn_jni_string__svn_to_j(JNIEnv *env, 
                         svn_string_t *string, 
                         jboolean *hasException);

/* svn_jni_misc */
void
svn_jni_misc__throw_exception_by_name(JNIEnv *env,
				      const char *name,
				      const char *msg);

svn_client_auth_baton_t *
svn_jni_misc__make_auth_baton(JNIEnv *env, jobject jobj);



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
