#include <jni.h>
#include <apr_general.h>
#include <malloc.h>
#include <svn_pools.h>
#include "svn_jni.h"

/*
 * some local variables
 */
apr_pool_t *pool;

/*
 * some local help functions
 */
char *svn_jni__GetStringNativeChars(JNIEnv *env, jstring jstr)
{
  jbyteArray bytes = NULL;
  jthrowable exc;
  char *result = NULL;
  jint len;
  
  if( (*env)->EnsureLocalCapacity(env, 2) < 0)
    {
      return NULL; /* out of memory error */
    }

  len = (*env)->GetStringUTFLength(env, jstr);
  result = malloc(len + 1);

  if( result == NULL )
  {
    return NULL; /* out of memory error */
  }

  (*env)->GetStringUTFRegion(env, jstr, 0, len, result);

  return result;
}      

/*
 * JNI OnLoad Handler
 */
JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM *jvm, void *reserved)
{
  apr_initialize();
  pool = NULL;
  pool = svn_pool_create(NULL);

  return JNI_VERSION_1_2;
  
}

/*
 * JNI UnLoad Handler
 */
JNIEXPORT OnUnload(JavaVM *jvm, void *reserved)
{
  apr_terminate();

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
  char *c_path = svn_jni__GetStringNativeChars(env, path);
  char *c_recursive = "";

  if( recursive == JNI_TRUE )
    {
      c_recursive = " -r ";
    }
 
  printf("command: svn add%s%s\n", c_recursive, c_path);
  printf("doing nothing yet!\n");

  if( c_path != NULL )
    {
      free(c_path);
    }
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

JNIEXPORT jobject JNICALL 
Java_org_tigris_subversion_lib_ClientImpl_status
  (JNIEnv *env, jobject obj, jstring path, jboolean descend,
   jboolean get_all, jboolean update)
{
  printf("svn_client_status doing nothing at all\n");
  
  return NULL; 
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

