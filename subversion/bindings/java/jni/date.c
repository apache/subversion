/*
 * utility functions to handle the java class
 * java.util.Date
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
#include "j.h"
#include "global.h"
#include "date.h"

/*** Defines ***/
#define SVN_JNI_DATE__CLASS "java/util/Date"
#define SVN_JNI_DATE__SIG "(J)V"

//DO YOU WANT TO DEBUG THIS CODE?
//JUST UNCOMMENT THE FOLLOWING LINE
//#define SVN_JNI_DATE__DEBUG

jobject 
date__create(JNIEnv *env, jboolean *hasException, jlong time)
{
  jobject result = NULL;
  jboolean _hasException = JNI_FALSE;

#ifdef SVN_JNI_DATE__DEBUG
  fprintf(stderr, ">>>date__create(");
  SVN_JNI__DEBUG_LONG(time);
  fprintf(stderr, ")\n");
#endif

  /*
   * references needed:
   * - class
   * - constructor
   * - object
   * = 3
   */
  if( (*env)->PushLocalFrame(env, 3) < 0 )
    {
      /* failed */
      _hasException = JNI_TRUE;
    }
  else
    {
      jclass class = NULL;
      jmethodID constructor = NULL;
      jobject jdate = NULL;

      /* get class reference */
      class = j__get_class(env, &_hasException,
                          SVN_JNI_DATE__CLASS);

      /* get method reference */
      if( !_hasException )
	{
	  constructor = 
            j__get_method(env, &_hasException,
                          class,
                          "<init>",
                          SVN_JNI_DATE__SIG);
	}

      /* create new instance */
      if( !_hasException )
	{
	  /* the apr_time_t parameter time may be passed
	   * directly to the java.util.Date(long) constructor
	   */
	  result = (*env)->NewObject(env, class, constructor, time);

	  if( result == NULL )
	    {
	      _hasException = JNI_TRUE;
	    }
	}

      (*env)->PopLocalFrame(env, result);
    }

#ifdef SVN_JNI_DATE__DEBUG
  fprintf(stderr, "\n<<<date__create(");
  SVN_JNI__DEBUG_BOOL(_hasException);
  fprintf(stderr, ")\n");
#endif
				    
  if( (hasException != NULL) && _hasException )
    {
      *hasException = JNI_TRUE;
    }

  return result;
}

jobject
date__create_from_apr_time_t(JNIEnv *env, jboolean *hasException, 
                             apr_time_t time)
{
  /*
   * convert from microseconds since 1970-01-01 00:00:00 GMT
   * to milliseconds since 1970-01-01 00:00:00 GMT
   * which is a simple integer division
   */
  jlong milliseconds = time / 1000;
  return date__create(env, hasException, milliseconds);
}
