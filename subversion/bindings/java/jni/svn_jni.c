/*
 * svn_jni.c:  native implementation of the functions declared in
 *             the Java class org.tigris.subversion.lib.ClientImpl
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

/* ==================================================================== */



/*** Includes. ***/

#include <jni.h>
#include <apr_general.h>
#include <malloc.h>
#include <svn_pools.h>
#include <svn_client.h>
#include "svn_jni.h"
#include "svn_jni_global.h"

/*** Defines. ***/
#define SVN_JNI__VERBOSE

#define SVN_JNI__SUBVERSION_EXCEPTION "/org/tigris/subversion/SubversionException"
#define SVN_JNI__ERROR_CREATE_STRINGBUF "error while creating stringbuf_t"
#define SVN_JNI__ERROR_CLIENT_STATUS "error in svn_client_status()"

/*
 * some local variables
 */
apr_pool_t *svn_jni__pool;



/*** Code. ***/


/*
 * JNI OnLoad Handler
 */
JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM *jvm, void *reserved)
{
#ifdef SVN_JNI__VERBOSE
  fprintf(stderr, "JNI_OnLoad\n");
#endif
  apr_initialize();
  svn_jni__pool = NULL;
  svn_jni__pool = svn_pool_create(NULL);

  return JNI_VERSION_1_2;
  
}

/*
 * JNI UnLoad Handler
 */
JNIEXPORT OnUnload(JavaVM *jvm, void *reserved)
{
#ifdef SVN_JNI__VERBOSE
    fprintf(stderr, "JNI_OnUnload\n");
#endif
  apr_terminate();

}

JNIEXPORT void JNICALL 
Java_org_tigris_subversion_lib_ClientImpl_checkout
  (JNIEnv *env, jobject beforeEditor, jobject obj, 
  jobject afterEditor, jstring url, jstring path, jobject revision, 
  jobject time, jstring xml_src)
{
}

JNIEXPORT void JNICALL 
Java_org_tigris_subversion_lib_ClientImpl_update
  (JNIEnv *env, jobject obj, jobject beforeEditor, 
  jobject afterEditor, jstring path, jstring xml_src, 
  jstring revision, jobject time)
{
}

JNIEXPORT void JNICALL 
Java_org_tigris_subversion_lib_ClientImpl_add
  (JNIEnv *env, jobject obj, jstring path, jboolean recursive)
{
}

JNIEXPORT void JNICALL 
Java_org_tigris_subversion_lib_ClientImpl_delete
  (JNIEnv *env, jobject obj, jstring path, jboolean force)
{
}

JNIEXPORT void JNICALL 
Java_org_tigris_subversion_lib_ClientImpl_performImport
  (JNIEnv *env, jobject obj, jobject beforeEditor, 
  jobject afterEditor, jstring path, jstring url, 
  jstring new_entry, jstring log_msg, jstring xml_dst, jstring revision)
{
}

JNIEXPORT void JNICALL 
Java_org_tigris_subversion_lib_ClientImpl_commit
  (JNIEnv *env, jobject obj, jobject beforeEditor, 
  jobject afterEditor, jobjectArray targets, 
  jstring log_msg, jstring xml_dst, jstring revision)
{
}

JNIEXPORT jobject JNICALL 
Java_org_tigris_subversion_lib_ClientImpl_status
  (JNIEnv *env, jobject jobj, jstring jtarget, jboolean jdescend,
   jboolean jget_all, jboolean jupdate)
{
  jobject hashtable = NULL;
  jboolean hasException = JNI_FALSE;
  svn_string_t *target_string = NULL;
  svn_stringbuf_t *target_stringbuf = NULL;
  svn_boolean_t descend = jdescend == JNI_TRUE;
  svn_boolean_t get_all = jget_all == JNI_TRUE;
  svn_boolean_t update = jupdate == JNI_TRUE;
  apr_hash_t *statushash;
  svn_client_auth_baton_t *auth_baton = NULL;

#ifdef SVN_JNI__VERBOSE
  fprintf(stderr, 
	  "Java_org_tigris_subversion_lib_ClientImpl_status\n");
#endif

  /* do all the type conversion stuff */
  target_string = svn_jni_string__j_to_svn(env, 
                                           jtarget, &hasException,
                                           svn_jni__pool);

  if( !hasException )
    {
      target_stringbuf = 
	svn_stringbuf_create_from_string(target_string, 
					 svn_jni__pool);

      if( target_stringbuf == NULL )
	{
	  /* seems like the conversion didnt succeed */
	  hasException = JNI_TRUE;
	  svn_jni__throw_exception_by_name(env, 
					   SVN_JNI__SUBVERSION_EXCEPTION,
					   SVN_JNI__ERROR_CREATE_STRINGBUF);
	}

    }

  if( !hasException )
    {
      auth_baton = svn_jni_misc__make_auth_baton(env, jobj);

      if( svn_client_status(&statushash, target_stringbuf, auth_baton,
			    descend, get_all, update, 
			    svn_jni__pool) < 0 
	  )
	{
	  /* in the case of an error, throw a java exception */
	  hasException = JNI_TRUE;
	  svn_jni__throw_exception_by_name(env, 
					   SVN_JNI__SUBVERSION_EXCEPTION,
					   SVN_JNI__ERROR_CLIENT_STATUS);
	}
    }

  if( !hasException )
    {
      if( (*env)->PushLocalFrame(env, 1) >= 0 )
	{
	  jboolean hasException = JNI_FALSE;
	  jobject hashtable = svn_jni_hashtable__create(env, &hasException);

	  /* now we do have a fresh new hashtable */
	  if( !hasException )
	    {
	      /* iterate through apr hashtable and
	       * insert each item into java hashtable */
	      apr_hash_index_t *index = apr_hash_first(svn_jni__pool, 
						       statushash);
	      while( (index != NULL) && (!hasException ) )
		{
		  svn_item_t *item;
		  char *path;
		  svn_wc_status_t *status;
		  jobject jitem = NULL;
		  jstring jpath = NULL;
		  jobject jstatus = NULL;

		  apr_hash_this(index, NULL, NULL, (void*)&item);
		  path = item->key;
		  status = item->data;

		  /* convert native string to java string */
		  jpath = svn_jni_string_c_to_j(env, path, &hasException);

		  /* convert svn_wc_status_t to java class Status */
		  if( !hasException )
		    {
		      jstatus = svn_jni_status__create(env, status, 
						       &hasException);
		    }

		  /* now create the java class Item */
		  if( !hasException )
		    {
		      jitem = svn_jni_item__create(env,
						   jpath,
						   jstatus,
						   &hasException);
		    }

		  /* put entry into java hashtable */
		  if( !hasException )
		    {
		      svn_jni__hashtable_put(env, hashtable, jpath, jitem, 
					     &hasException);
		    }
		  
		  if( !hasException )
		    {
		      /* proceed to the next iteration */
		      apr_hash_next(index);
		    }
		} /* while( ... ) */
	    } /* if( !_hasException ) */

	  (*env)->PopLocalFrame(env, hashtable);
	} /* if( ... Push ... ) */
    }

  return hashtable;
}

JNIEXPORT jstring JNICALL 
Java_org_tigris_subversion_lib_ClientImpl_fileDiff
  (JNIEnv *env, jobject obj, jstring path)
{
  printf("doing nothing at all\n");
}

JNIEXPORT void JNICALL 
Java_org_tigris_subversion_lib_ClientImpl_cleanup
  (JNIEnv *env, jobject obj, jstring dir)
{
  printf("doing nothing at all\n");

}

/* 
 * local variables:
 * eval: (load-file "../../../svn-dev.el")
 * end: */






















