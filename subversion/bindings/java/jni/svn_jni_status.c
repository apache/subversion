/*
 * status.c utility functions to handle the java class
 * org.tigris.subversion.lib.Status
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
#include <svn_wc.h>

#define SVN_JNI_STATUS__CONSTRUCTOR \
"(Lorg/tigris/subversion/lib/Entry;IIIZII)V"

jobject
svn_jni_status__create(JNIEnv *env, svn_wc_status_t *status, 
		       jboolean *hasException)
{
  jobject jstatus = NULL;
  jobject jentry = NULL;
  jboolean _hasException = JNI_FALSE;

#ifdef SVN_JNI__VERBOSE
  fprintf(stderr, "svn_jni__create_status\n");
#endif

  if( (*env)->PushLocalFrame(env, 4) >= 0 )
    {
      jclass statusClass = NULL;
      jmethodID statusConstructor = NULL;

      statusClass = (*env)->FindClass(env, 
				      "org/tigris/subversion/lib/Status");
      if( statusClass == NULL )
	{
	  _hasException = JNI_TRUE;
	}
      else
	{
	  statusConstructor = 
	      (*env)->GetMethodID(env, statusClass,
				  "<init>", SVN_JNI_STATUS__CONSTRUCTOR);

	  if( statusConstructor == NULL )
	  {
	      _hasException = JNI_TRUE;
	  }
	}

      if( !_hasException )
        {
            //
        }

      if( !_hasException )
        {
          jstatus = (*env)->NewObject(env, statusClass, 
                                      statusConstructor,
                                      jentry,
                                      status->repos_rev,
                                      status->text_status,
                                      status->prop_status,
                                      status->locked,
                                      status->repos_text_status,
                                      status->repos_prop_status);
        }

      (*env)->PopLocalFrame(env, jstatus);
    }


  if( hasException != NULL )
    {
      *hasException = _hasException;
    }

  return jstatus;
}

/* 
 * local variables:
 * eval: (load-file "../../../svn-dev.el")
 * end: */

