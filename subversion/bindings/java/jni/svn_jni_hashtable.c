/*
 * svn_jni_hashtable.c utility functions to handle 
 * java hashtables with c
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

#include <jni.h>

#define SVN_JNI__HASHTABLE_PUT \
"(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;"

jobject
svn_jni_hashtable__create(JNIEnv *env, jboolean *hasException)
{
  jobject hashtable = NULL;
  jboolean _hasException = JNI_FALSE;

#ifdef SVN_JNI__VERBOSE
  fprintf(stderr, "svn_jni__create_hashtable\n");
#endif
  
  /* is there enough memory to have twoadditional
   * local references? 
   * - class reference
   * - constructor method id
   */
  if( (*env)->PushLocalFrame(env, 3) >= 0 )
    {
      jclass hashtableClass = (*env)->FindClass(env,
						"java/util/Hashtable");
      jmethodID hashtableConstructor = NULL;
      
      if( hashtableClass == NULL )
	{
	  _hasException = JNI_TRUE;
	}
      else
	{
	  hashtableConstructor = 
	    (*env)->GetMethodID(env, hashtableClass,
				"<init>", "()V");
	}

      if( hashtableConstructor == NULL )
	{
	  _hasException = JNI_TRUE;
	}
      else
	{
	  hashtable = (*env)->NewObject(env, hashtableClass,
					hashtableConstructor);
	}

      if( hashtable == NULL )
	{
	  _hasException = JNI_TRUE;
	}

      /* pop local frame but preserve the newly create hashtable */
      (*env)->PopLocalFrame(env, hashtable);
    }

  /* return wether an exception has occured */
  if( hasException != NULL )
    {
      (*hasException) = _hasException;
    }

  return hashtable;
}

void
svn_jni_hashtable__put(JNIEnv *env, jobject hashtable, jobject key,
		      jobject value, jboolean *hasException)
{
  jboolean _hasException = JNI_FALSE;

#ifdef SVN_JNI__VERBOSE
  fprintf(stderr, "svn_jni__hashtable_put\n");
#endif

  /* enough space for two local references?
   * - class reference
   * - method id
   */
  if( (*env)->PushLocalFrame(env, 2) >= 0 )
    {
      jclass hashtableClass = NULL;
      jmethodID hashtablePut = NULL;

      hashtableClass = (*env)->FindClass(env, "java/util/Hashtable");

      if( hashtableClass == NULL )
	{
	  _hasException = JNI_TRUE;
	}
      else
	{
	  hashtablePut = 
	    (*env)->GetMethodID(env, hashtableClass, 
				"put", SVN_JNI__HASHTABLE_PUT);
	  if( hashtablePut == NULL )
	    {
	      _hasException = JNI_TRUE;
	    }
	}

      if( hashtablePut != NULL )
	{
	  /* the put method usually returns an object
	   * but we dont care about this so we dont have
	   * to take care for the otherweise created
	   * local reference 
	   */
	  (*env)->CallVoidMethod(env, hashtable, hashtablePut,
				   key, value);
	  _hasException = (*env)->ExceptionCheck(env);
	}

      /* pop local references */
      (*env)->PopLocalFrame(env, 0);
    }

  /* check wether an exception has occured */
  if( hasException != NULL )
    {
      (*hasException) = _hasException;
    }
} 

/* local variables:
 * eval: (load-file "../../../svn-dev.el")
 * end: */
