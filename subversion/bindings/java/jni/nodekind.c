/*
 * utility functions to handle the java class
 * org.tigris.subversion.lib.Nodekind
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
#include "nodekind.h"

/*** Defines ***/
#define SVN_JNI_NODEKIND__CLASS "org/tigris/subversion/lib/Nodekind"
#define SVN_JNI_NODEKIND__SIG "(I)V"

//DO YOU WANT TO DEBUG THIS CODE?
//JUST UNCOMMENT THE FOLLOWING LINE
//#define SVN_JNI_NODEKIND__DEBUG

jobject 
nodekind__create(JNIEnv *env, jboolean *hasException, jint nodekind)
{
  jobject result = NULL;
  jboolean _hasException = JNI_FALSE;

#ifdef SVN_JNI_NODEKIND__DEBUG
  fprintf(stderr, ">>>nodekind__create(");
  SVN_JNI__DEBUG_DEC(nodekind);
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
      jobject jnodekind = NULL;

      /* get class reference */
      class = j__get_class(env, &_hasException,
                          SVN_JNI_NODEKIND__CLASS);

      /* get method reference */
      if( !_hasException )
	{
	  constructor = 
            j__get_method(env, &_hasException,
                          class,
                          "<init>",
                          SVN_JNI_NODEKIND__SIG);
	}

      /* create new instance */
      if( !_hasException )
	{
	  result = (*env)->NewObject(env, class, constructor, 
                                     nodekind);

	  if( result == NULL )
	    {
	      _hasException = JNI_TRUE;
	    }
	}

      (*env)->PopLocalFrame(env, result);
    }

#ifdef SVN_JNI_NODEKIND__DEBUG
  fprintf(stderr, "\n<<<nodekind__create(");
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
nodekind__create_from_svn_node_kind(JNIEnv *env, jboolean *hasException,
                                   svn_node_kind_t nodekind)
{
  /**
   * right now, all we do is cast the enum to an int. thats about it
   * feel free to improve things...
   */

  return nodekind__create(env, hasException, (jint)nodekind);

}
