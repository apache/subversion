/*
 * utility functions to handle the java class java.util.Hashtable
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

/*** Includes ***/
#include <jni.h>
#include "global.h"
#include "j.h"

/*** Defines ***/
#define SVN_JNI_HASHTABLE__CLASS "java/util/Hashtable"
#define SVN_JNI_HASHTABLE__CONSTRUCTOR "<init>"
#define SVN_JNI_HASHTABLE__CONSTRUCTOR_SIG "()V"
#define SVN_JNI_HASHTABLE__PUT "put"
#define SVN_JNI_HASHTABLE__PUT_SIG \
"(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;"

/*
 * Do you want to debug code in this file?
 * Just uncomment the following define.
 */
//#define SVN_JNI_HASHTABLE__DEBUG

/*** Code ***/
jobject
hashtable__create(JNIEnv *env, jboolean *hasException)
{
  jobject hashtable = NULL;
  jboolean _hasException = JNI_FALSE;

#ifdef SVN_JNI_HASHTABLE__DEBUG
  fprintf(stderr, ">>>hashtable__create\n");
#endif
  
  /* is there enough memory to have twoadditional
   * local references? 
   * - class reference
   * - constructor method id
   */
  if( (*env)->PushLocalFrame(env, 3) < 0 )
    {
      _hasException = JNI_TRUE;
    }
  else
    {
      jclass hashtableClass = NULL;
      jmethodID hashtableConstructor = NULL;

      hashtableClass = j__get_class(env, &_hasException,
                                    SVN_JNI_HASHTABLE__CLASS);

      if( !_hasException )
	{
	  hashtableConstructor = 
            j__get_method(env, &_hasException, 
                          hashtableClass,
                          SVN_JNI_HASHTABLE__CONSTRUCTOR,
                          SVN_JNI_HASHTABLE__CONSTRUCTOR_SIG);
	}

      if( !_hasException )
	{
	  hashtable = (*env)->NewObject(env, hashtableClass,
					hashtableConstructor);
	}

      if( hashtable == NULL )
	{
	  _hasException = JNI_TRUE;
	}

#ifdef SVN_JNI_HASHTABLE__DEBUG
      SVN_JNI__DEBUG_PTR(hashtableClass);
      SVN_JNI__DEBUG_PTR(hashtableConstructor);
      SVN_JNI__DEBUG_PTR(hashtable);
#endif

      /* pop local frame but preserve the newly create hashtable */
      (*env)->PopLocalFrame(env, hashtable);
    }


#ifdef SVN_JNI_HASHTABLE__DEBUG
  SVN_JNI__DEBUG_BOOL(_hasException);
  fprintf(stderr, "\n<<<hashtable__create\n");
#endif
              
  /* return wether an exception has occured */
  if( (hasException != NULL) && _hasException )
    {
      (*hasException) = JNI_TRUE;
    }

  return hashtable;
}

jobject
hashtable__put(JNIEnv *env, jobject hashtable, jobject key,
               jobject value, jboolean *hasException)
{
  jboolean _hasException = JNI_FALSE;
  jobject result = NULL;

#ifdef SVN_JNI_HASHTABLE__DEBUG
  fprintf(stderr, ">>>hashtable__put(");
  SVN_JNI__DEBUG_PTR(hashtable);
  SVN_JNI__DEBUG_PTR(key);
  SVN_JNI__DEBUG_PTR(value);
  fprintf(stderr, ")\n");
#endif

  /* enough space for two local references?
   * - class reference
   * - method id
   */
  if( (*env)->PushLocalFrame(env, 2) >= 0 )
    {
      jclass hashtableClass = NULL;
      jmethodID hashtablePut = NULL;

      hashtableClass = j__get_class(env, &_hasException, 
                                    SVN_JNI_HASHTABLE__CLASS);

      if( !_hasException )
        {
          hashtablePut = j__get_method(env, &_hasException,
                                       hashtableClass,
                                       SVN_JNI_HASHTABLE__PUT,
                                       SVN_JNI_HASHTABLE__PUT_SIG);
        }

      if( !_hasException )
        {
#ifdef SVN_JNI_HASHTABLE__DEBUG
          fprintf(stderr, ">>>CallObjectMethod(");
          SVN_JNI__DEBUG_PTR(hashtable);
          SVN_JNI__DEBUG_PTR(hashtablePut);
          SVN_JNI__DEBUG_PTR(key);
          SVN_JNI__DEBUG_PTR(value);
          fprintf(stderr, ")\n");
#endif

	  result = (*env)->CallObjectMethod(env, hashtable, hashtablePut,
                                            key, value);
	  _hasException = (*env)->ExceptionCheck(env);
#ifdef SVN_JNI_HASHTABLE__DEBUG
          fprintf(stderr, "<<<CallObjectMethod(");
          SVN_JNI__DEBUG_PTR(result);
          SVN_JNI__DEBUG_BOOL(_hasException);
          fprintf(stderr, ")\n");
#endif
	}

      /* pop local references */
      (*env)->PopLocalFrame(env, result);
    }

#ifdef SVN_JNI_HASHTABLE__DEBUG
  fprintf(stderr, "\n<<<hashtable__put(");
  SVN_JNI__DEBUG_BOOL(_hasException);
  fprintf(stderr, ")\n");
#endif

  /* check wether an exception has occured */
  if( (hasException != NULL) && _hasException )
    {
      (*hasException) = JNI_TRUE;
    }

  return result;
} 
