/*
 * generic functions to handle java classes
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

#ifndef SVN_JNI_J_H
#define SVN_JNI_J_H

/* includes */
#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* functions */

/* returns a JNI class reference matching 
 * className. 
 *
 * Remark: you have to ensure that there
 * is enough space for the class reference
 * (needs 1 reference)
 */
jclass
j__get_class(JNIEnv *env, jboolean *hasException, 
             char *className);

/* return a JNI method reference matching the
 * class, methodName and methodSignature
 *
 * Remark: you have to ensure that there
 * is enoug space for the class reference
 * (needs
 */
jmethodID
j__get_method(JNIEnv *env, jboolean *hasException,
              jclass class,
              char *methodName, char *methodSignature);

void 
j__set_boolean(JNIEnv *env, jboolean *hasException,
               char *className, char *methodName,
               jobject obj, jboolean value);

jboolean 
j__get_boolean(JNIEnv *env, jboolean *hasException,
               char *className, char *methodName,
               jobject obj);

void 
j__set_int(JNIEnv *env, jboolean *hasException,
           char *className, char *methodName,
           jobject obj, jint value);

void 
j__set_long(JNIEnv *env, jboolean *hasException,
            char *className, char *methodName,
            jobject obj, jlong value);

void 
j__set_object(JNIEnv *env, jboolean *hasException,
              char *className, char *methodName,
              char *methodSig,
              jobject obj, jobject value);

jobject 
j__get_object(JNIEnv *env, jboolean *hasException,
              char *className, char *methodName,
              char *methodSig,
              jobject obj);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif

/* 
 * local variables:
 * eval: (load-file "../../../../tools/dev/svn-dev.el")
 * end: 
 */
