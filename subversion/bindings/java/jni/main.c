/*
 * native implementation of the functions declared in
 * the Java class org.tigris.subversion.lib.ClientImpl
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

/* ==================================================================== */



/*** Includes. ***/

#include <jni.h>
#include <apr_general.h>
#include <malloc.h>
#include <svn_pools.h>
#include <svn_client.h>
#include <svn_string.h>
#include "svn_jni.h"
#include "global.h"
#include "status.h"
#include "string.h"
#include "misc.h"
#include "hashtable.h"
#include "entry.h"

/*** Defines. ***/

/*
 * Do you want to debug code in this file?
 * Just uncomment the following define
 */
//#define SVN_JNI_MAIN__DEBUG




/*** Code. ***/


/*
 * JNI OnLoad Handler
 */
JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM *jvm, void *reserved)
{
#ifdef SVN_JNI_MAIN__DEBUG
  fprintf(stderr, ">>>JNI_OnLoad\n");
#endif
  apr_initialize();

#ifdef SVN_JNI_MAIN__DEBUG
  fprintf(stderr, "\n<<<JNI_OnLoad\n");
#endif

  return JNI_VERSION_1_2;
}

/*
 * JNI UnLoad Handler
 */
JNIEXPORT OnUnload(JavaVM *jvm, void *reserved)
{
#ifdef SVN_JNI_MAIN__DEBUG
  fprintf(stderr, ">>>JNI_OnUnload\n");
#endif
  apr_terminate();
#ifdef SVN_JNI_MAIN__DEBUG
  fprintf(stderr, "\n<<<JNI_OnUnload\n");
#endif

}

JNIEXPORT void JNICALL 
Java_org_tigris_subversion_lib_ClientImpl_checkout
  (JNIEnv *env, jobject beforeEditor, jobject obj, 
  jobject afterEditor, jstring url, jstring path, jobject revision, 
  jobject time, jstring xml_src)
{
}

JNIEXPORT void JNICALL 
Java_org_tigris_subversion_lib_ClientImpl_update
  (JNIEnv *env, jobject obj, jobject beforeEditor, 
  jobject afterEditor, jstring path, jstring xml_src, 
  jstring revision, jobject time)
{
}

JNIEXPORT void JNICALL 
Java_org_tigris_subversion_lib_ClientImpl_add
  (JNIEnv *env, jobject obj, jstring path, jboolean recursive)
{
}

JNIEXPORT void JNICALL 
Java_org_tigris_subversion_lib_ClientImpl_delete
  (JNIEnv *env, jobject obj, jstring path, jboolean force)
{
}

JNIEXPORT void JNICALL 
Java_org_tigris_subversion_lib_ClientImpl_performImport
  (JNIEnv *env, jobject obj, jobject beforeEditor, 
  jobject afterEditor, jstring path, jstring url, 
  jstring new_entry, jstring log_msg, jstring xml_dst, jstring revision)
{
}

JNIEXPORT void JNICALL 
Java_org_tigris_subversion_lib_ClientImpl_commit
  (JNIEnv *env, jobject obj, jobject beforeEditor, 
  jobject afterEditor, jobjectArray targets, 
  jstring log_msg, jstring xml_dst, jstring revision)
{
}


JNIEXPORT jstring JNICALL 
Java_org_tigris_subversion_lib_ClientImpl_fileDiff
  (JNIEnv *env, jobject obj, jstring path)
{
  printf("doing nothing at all\n");
}

JNIEXPORT void JNICALL 
Java_org_tigris_subversion_lib_ClientImpl_cleanup
  (JNIEnv *env, jobject obj, jstring dir)
{
  printf("doing nothing at all\n");

}
