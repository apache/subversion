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

#include <jni.h>

jclass
svn_jni_j__get_class(JNIEnv *env, jboolean *hasException, 
                     char *className)
{
  jclass result = NULL;
  jboolean _hasException = JNI_FALSE;

  /*
   * references needed:
   * class
   */
  if( (*env)->PushLocalFrame(env, 1) < 0 )
    {
      _hasException = JNI_TRUE;
    }
  else
    {
      result = (*env)->FindClass(env, className);
      if( result == NULL )
	{
	  _hasException = JNI_TRUE;
	}

      (*env)->PopLocalFrame(env, result);
    }

  if( hasException != NULL )
    {
      *hasException = _hasException;
    }

  return result;
}

jmethodID
svn_jni_j__get_method(JNIEnv *env, jboolean *hasException,
                      jclass class,
                      char *methodName, char *methodSignature)
{
  jmethodID result = NULL;
  jboolean _hasException = JNI_FALSE;

  /*
   * references needed:
   * - methodID
   */
  if( (*env)->PushLocalFrame(env, 1) < 0 )
    {
      /* failed */
      _hasException = JNI_TRUE;
    }
  else
    {
      result = (*env)->GetMethodID(env, class, methodName, 
                                   methodSignature);

      if( result == NULL )
        {
          _hasException = JNI_TRUE;
        }

      (*env)->PopLocalFrame(env, result);
    }

  if( hasException != NULL )
    {
      *hasException = _hasException;
    }

  return result;
}

/* 
 *local variables:
 * eval: (load-file "../../../svn-dev.el")
 * end: 
 */

