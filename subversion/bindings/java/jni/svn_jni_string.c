/*
 * svn_jni_string.c utility functions to deal with java strings
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

/* includes */
#include <jni.h>
#include <svn_string.h>

svn_string_t *
svn_jni_string__j_to_svn(JNIEnv *env, 
                         jstring jstr, 
                         jboolean *hasException,
                         apr_pool_t *pool)
{
  svn_string_t *result = NULL;
  jboolean _hasException = JNI_FALSE;

#ifdef SVN_JNI__VERBOSE
  fprintf(stderr, "svn_jni_string__j_to_svn\n");
#endif
  
  /* make sure there is enough memory left for 
   * the operation, also push the stack frame
   * we will need 2 local references:
   * - 
   */
  if( (*env)->PushLocalFrame(env, 2) >= 0)
    {
      jbyteArray bytes = NULL;

      /* check for exception */
      _hasException = (*env)->ExceptionCheck(env);

      if( !_hasException )
	{
	  char *buffer;
	  jsize len = (*env)->GetStringUTFLength(env, jstr);
	  buffer = (char *)malloc(len + 1);

	  /* did the memory allocation succeed? 
	   * otherwise throw an exception */
	  if( buffer == NULL )
	    {
	      svn_jni__throw_exception_by_name(env, 
					       "java/lang/OutOfMemoryError", 
					       NULL);
	      _hasException = JNI_TRUE;
	    }
	  else
	    {
	      (*env)->GetStringUTFRegion(env, jstr, 0, len, buffer);
	      buffer[len] = 0;

	      if( (*env)->ExceptionCheck(env) )
		{
		  _hasException = JNI_TRUE;
		}

	      if( !_hasException )
		{
		  result = svn_string_create(buffer, pool);
		}

	      /* ...and release the buffer */
	      free(buffer);
	    }
	} /* if( !_hasException ) */

      (*env)->PopLocalFrame(env, NULL);

    }

  /* return wether an exception has occured */
  if( hasException != NULL )
    {
      (*hasException) = _hasException;
    }

  return result;
}

jstring
svn_jni_string__c_to_j(JNIEnv *env, 
                       char *string, 
                       jboolean *hasException)
{
  jboolean _hasException = JNI_FALSE;
  jstring result = NULL;

  result = (*env)->NewStringUTF(env, string);

  if( (*env)->ExceptionCheck(env) )
    {
      _hasException = JNI_TRUE;
    }

  if( hasException != NULL )
    {
      (*hasException) = _hasException;
    }

  return result;
}

jstring
svn_jni_string__svn_to_j(JNIEnv *env, 
                         svn_string_t *string, 
                         jboolean *hasException)
{
  return svn_jni_string__c_to_j(env, (char*)string->data, 
                                hasException);
}

/* 
 * local variables:
 * eval: (load-file "../../../svn-dev.el")
 * end: 
 */





