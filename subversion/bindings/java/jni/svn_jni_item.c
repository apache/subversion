/*
 * svn_jni_item.c utility functions to handle the class
 * org.tigris.subversion.lib.Item
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

#include <jni.h>
#include <svn_client.h>

#define SVN_JNI_ITEM__CONSTRUCTOR \
"(Ljava/lang/Object;Ljava/lang/Object;)V"

/*
 * utility function to create a java Item
 */
jobject
svn_jni_item__create(JNIEnv *env, jobject jpath, jobject jstatus, 
		     jboolean *hasException)
{
  jobject jitem = NULL;
  jboolean _hasException = JNI_FALSE;

#ifdef SVN_JNI__VERBOSE
  fprintf(stderr, "svn_jni__create_item\n");
#endif

  if( (*env)->PushLocalFrame(env,4) )
    {
      jclass itemClass = NULL;
      jmethodID itemConstructor = NULL;
      
      itemClass = (*env)->FindClass(env,
				    "org/tigris/subversion/lib/Item");
      if( itemClass == NULL )
	{
	  _hasException = JNI_TRUE;
	}
      else
	{
	    itemConstructor = (*env)->GetMethodID(env, itemClass,
						  "<init>",
						  SVN_JNI_ITEM__CONSTRUCTOR);

	    if( itemConstructor == NULL )
	    {
		_hasException = JNI_TRUE;
	    }

	    /* here has to be some code to call the constructor
	       and do stuff */
	}

      (*env)->PopLocalFrame(env, jitem);
    }

  if( hasException != NULL )
  {
      *hasException = _hasException;
  }

  return jitem;
}
