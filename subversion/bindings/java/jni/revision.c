/*
 * utility functions to handle the java class
 * org.tigris.subversion.lib.Revision
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
#include "revision.h"

/*** Defines ***/
#define SVN_JNI_REVISION__CLASS "org/tigris/subversion/lib/Revision"
#define SVN_JNI_REVISION__SIG "(J)V"

//DO YOU WANT TO DEBUG THIS CODE?
//JUST UNCOMMENT THE FOLLOWING LINE
//#define SVN_JNI_REVISION__DEBUG

jobject 
revision__create(JNIEnv *env, jboolean *hasException, jlong revision)
{
  jobject result = NULL;
  jboolean _hasException = JNI_FALSE;

#ifdef SVN_JNI_REVISION__DEBUG
  fprintf(stderr, ">>>revision__create(");
  SVN_JNI__DEBUG_LONG(revision);
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
      jobject jrevision = NULL;

      /* get class reference */
      class = j__get_class(env, &_hasException,
                          SVN_JNI_REVISION__CLASS);

      /* get method reference */
      if( !_hasException )
	{
	  constructor = 
            j__get_method(env, &_hasException,
                          class,
                          "<init>",
                          SVN_JNI_REVISION__SIG);
	}

      /* create new instance */
      if( !_hasException )
	{
	  result = (*env)->NewObject(env, class, constructor, 
                                     revision);

	  if( result == NULL )
	    {
	      _hasException = JNI_TRUE;
	    }
	}

      (*env)->PopLocalFrame(env, result);
    }

#ifdef SVN_JNI_REVISION__DEBUG
  fprintf(stderr, "\n<<<revision__create(");
  SVN_JNI__DEBUG_BOOL(_hasException);
  fprintf(stderr, ")\n");
#endif
				    
  if( (hasException != NULL) && _hasException )
    {
      *hasException = JNI_TRUE;
    }

  return result;
}
