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

/*** Code ***/

jclass
j__get_class(JNIEnv *env, jboolean *hasException, 
             char *className)
{
  jclass result = NULL;
  jboolean _hasException = JNI_FALSE;

#ifdef SVN_JNI__VERBOSE
  fprintf(stderr,"j__get_class(classname=%s)\n", className);
#endif

  result = (*env)->FindClass(env, className);
  if( result == NULL )
    {
      _hasException = JNI_TRUE;
    }

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

#ifdef SVN_JNI__VERBOSE
  fprintf(stderr, "j__get_method(" 
          "methodName='%s';"
          "methodSignature='%s';",
          methodName, methodSignature);
  if( class == NULL )
    {
      fprintf(stderr, "class=NULL;");
    }
#endif

  result = (*env)->GetMethodID(env, class, methodName, 
                               methodSignature);
#ifdef SVN_JNI__VERBOSE
  if( result == NULL )
    {
      fprintf(stderr, "hasException;");
    }
  fprintf(stderr, ")\n");
#endif

  if( result == NULL )
    {
      _hasException = JNI_TRUE;
    }
  
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

#ifdef SVN_JNI__VERBOSE
  fprintf(stderr, "\nentry__set_int("
          "className='%s'; methodName='%s'; value=%d)\n{\n",
          className, methodName, value);
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

#ifdef SVN_JNI__VERBOSE
  if( _hasException )
    {
      fprintf(stderr, "hasException;");
    }
  fprintf(stderr, "\n}\n");
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

#ifdef SVN_JNI__VERBOSE
  fprintf(stderr, "\nentry__set_long("
          "className='%s'; methodName='%s'; value=%d)\n{\n",
          className, methodName, value);
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

#ifdef SVN_JNI__VERBOSE
  if( _hasException )
    {
      fprintf(stderr, "hasException;");
    }
  fprintf(stderr, "\n}\n");
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

#ifdef SVN_JNI__VERBOSE
  fprintf(stderr, "\nentry__set_boolean("
          "className='%s'; methodName='%s'; value=%d)\n{\n",
          className, methodName, value);
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

#ifdef SVN_JNI__VERBOSE
  if( _hasException )
    {
      fprintf(stderr, "hasException;");
    }
  fprintf(stderr, "\n}\n");
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

#ifdef SVN_JNI__VERBOSE
  fprintf(stderr, "\nentry__set_object("
          "className='%s'; methodName='%s'; "
          "methodSig=%s;"
          ")\n{\n",
          className, methodName, methodSig);
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

#ifdef SVN_JNI__VERBOSE
  if( _hasException )
    {
      fprintf(stderr, "hasException;");
    }
  fprintf(stderr, "\n}\n");
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


