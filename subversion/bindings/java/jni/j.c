/*
 * generic functions to handle java classes
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

/*** Includes ***/
#include <jni.h>
#include "global.h"

/*** Defines ***/
//DO YOU WANT TO DEBUG THIS CODE?
//JUST UNCOMMENT THE FOLLOWING LINE
//#define SVN_JNI_J__DEBUG

/*** Code ***/

jclass
j__get_class(JNIEnv *env, jboolean *hasException, 
             char *className)
{
  jclass result = NULL;
  jboolean _hasException = JNI_FALSE;

#ifdef SVN_JNI_J__DEBUG
  fprintf(stderr,">>>j__get_class(classname=%s)\n", className);
#endif

  result = (*env)->FindClass(env, className);
  if( result == NULL )
    {
      _hasException = JNI_TRUE;
    }

#ifdef SVN_JNI_J__DEBUG
  fprintf(stderr,"\n<<<j__get_class\n");
#endif

  if( hasException != NULL )
    {
      *hasException = _hasException;
    }

  return result;
}

jmethodID
j__get_method(JNIEnv *env, jboolean *hasException,
              jclass class,
              char *methodName, char *methodSignature)
{
  jmethodID result = NULL;
  jboolean _hasException = JNI_FALSE;

#ifdef SVN_JNI_J__DEBUG
  fprintf(stderr, ">>>j__get_method(");
  SVN_JNI__DEBUG_STR(methodName);
  SVN_JNI__DEBUG_STR(methodSignature);
  SVN_JNI__DEBUG_PTR(class);
  fprintf(stderr, ")\n");
#endif

  result = (*env)->GetMethodID(env, class, methodName, 
                               methodSignature);

  if( result == NULL )
    {
      _hasException = JNI_TRUE;
    }
  
#ifdef SVN_JNI_J__DEBUG
  SVN_JNI__DEBUG_PTR(result);
  SVN_JNI__DEBUG_BOOL(_hasException);
  fprintf(stderr, "\n<<<j__get_method\n");
#endif

  if( hasException != NULL )
    {
      *hasException = _hasException;
    }

  return result;
}

void j__set_int(JNIEnv *env, jboolean *hasException,
                char *className, char *methodName,
                jobject obj, jint value)
{
  jclass clazz = NULL;
  jmethodID methodID = NULL;
  jboolean _hasException = JNI_FALSE;

#ifdef SVN_JNI_J__DEBUG
  fprintf(stderr, ">>>j__set_int(");
  SVN_JNI__DEBUG_STR(className);
  SVN_JNI__DEBUG_STR(methodName);
  SVN_JNI__DEBUG_DEC(value);
  fprintf(stderr, ")\n");
#endif

  /*
   * needed references:
   * - class
   * - method
   * = 2 
   */
  if( (*env)->PushLocalFrame(env, 2) < 0 )
    {
      _hasException = JNI_TRUE;
    }
  else
    {
      clazz = j__get_class(env, &_hasException, className);

      if( !_hasException )
        {
          methodID = j__get_method(env, &_hasException,
                                   clazz, methodName, "(I)V");
        }

      if( !_hasException )
        {
          (*env)->CallVoidMethod(env, obj, methodID, value);

          _hasException = (*env)->ExceptionCheck(env);
        }

      (*env)->PopLocalFrame(env, NULL);
    }

#ifdef SVN_JNI_J__DEBUG
  SVN_JNI__DEBUG_BOOL(_hasException);
  fprintf(stderr, "\n<<<j__set_int\n");
#endif

  if( hasException != NULL )
    {
      *hasException = _hasException;
    }
}

void j__set_long(JNIEnv *env, jboolean *hasException,
                 char *className, char *methodName,
                 jobject obj, jlong value)
{
  jclass clazz = NULL;
  jmethodID methodID = NULL;
  jboolean _hasException = JNI_FALSE;

#ifdef SVN_JNI_J__DEBUG
  fprintf(stderr, ">>>j__set_long(");
  SVN_JNI__DEBUG_STR(className);
  SVN_JNI__DEBUG_STR(methodName);
  SVN_JNI__DEBUG_DEC(value);
  fprintf(stderr, ")\n");
#endif

  /*
   * needed references:
   * - class
   * - method
   * = 2 
   */
  if( (*env)->PushLocalFrame(env, 2) < 0 )
    {
      _hasException = JNI_TRUE;
    }
  else
    {
      clazz = j__get_class(env, &_hasException, className);

      if( !_hasException )
        {
          methodID = j__get_method(env, &_hasException,
                                   clazz, methodName, "(J)V");
        }

      if( !_hasException )
        {
          (*env)->CallVoidMethod(env, obj, methodID, value);

          _hasException = (*env)->ExceptionCheck(env);
        }

      (*env)->PopLocalFrame(env, NULL);
    }

#ifdef SVN_JNI_J__DEBUG
  SVN_JNI__DEBUG_BOOL(_hasException);
  fprintf(stderr, "\n<<<j__set_long\n");
#endif

  if( hasException != NULL )
    {
      *hasException = _hasException;
    }
}

void j__set_boolean(JNIEnv *env, jboolean *hasException,
                    char *className, char *methodName,
                    jobject obj, jboolean value)
{
  jclass clazz = NULL;
  jmethodID methodID = NULL;
  jboolean _hasException = JNI_FALSE;

#ifdef SVN_JNI_J__DEBUG
  fprintf(stderr, ">>>j__set_boolean(");
  SVN_JNI__DEBUG_STR(className);
  SVN_JNI__DEBUG_STR(methodName);
  SVN_JNI__DEBUG_DEC(value);
  fprintf(stderr, ")\n");
#endif

  /*
   * needed references:
   * - class
   * - method
   * = 2 
   */
  if( (*env)->PushLocalFrame(env, 2) < 0 )
    {
      _hasException = JNI_TRUE;
    }
  else
    {
      clazz = j__get_class(env, &_hasException, className);

      if( !_hasException )
        {
          methodID = j__get_method(env, &_hasException,
                                   clazz, methodName, "(Z)V");
        }

      if( !_hasException )
        {
          (*env)->CallVoidMethod(env, obj, methodID, value);

          _hasException = (*env)->ExceptionCheck(env);
        }

      (*env)->PopLocalFrame(env, NULL);
    }

#ifdef SVN_JNI_J__DEBUG
  SVN_JNI__DEBUG_BOOL(_hasException);
  fprintf(stderr, "\n<<<j__set_boolean\n");
#endif

  if( hasException != NULL )
    {
      *hasException = _hasException;
    }
}

void j__set_object(JNIEnv *env, jboolean *hasException,
                   char *className, char *methodName,
                   char *methodSig,
                   jobject obj, jobject value)
{
  jclass clazz = NULL;
  jmethodID methodID = NULL;
  jboolean _hasException = JNI_FALSE;

#ifdef SVN_JNI_J__DEBUG
  fprintf(stderr, ">>>j__set_object(");
  SVN_JNI__DEBUG_STR(className);
  SVN_JNI__DEBUG_STR(methodName);
  SVN_JNI__DEBUG_STR(methodSig);
  fprintf(stderr, ")\n");
#endif

  /*
   * needed references:
   * - class
   * - method
   * = 2 
   */
  if( (*env)->PushLocalFrame(env, 2) < 0 )
    {
      _hasException = JNI_TRUE;
    }
  else
    {
      clazz = j__get_class(env, &_hasException, className);

      if( !_hasException )
        {
          methodID = j__get_method(env, &_hasException,
                                   clazz, methodName, methodSig);
        }

      if( !_hasException )
        {
          (*env)->CallVoidMethod(env, obj, methodID, value);

          _hasException = (*env)->ExceptionCheck(env);
        }

      (*env)->PopLocalFrame(env, NULL);
    }

#ifdef SVN_JNI_J__DEBUG
  SVN_JNI__DEBUG_BOOL(_hasException);
  fprintf(stderr, "\n<<<j__set_object\n");
#endif

  if( hasException != NULL )
    {
      *hasException = _hasException;
    }
}

/* 
 * local variables:
 * eval: (load-file "../../../svn-dev.el")
 * end: 
 */








