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
#include "svn_jni.h"

/*** Defines. ***/
#define SVN_JNI__HASHTABLE_PUT "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;"

/*
 * some local variables
 */
apr_pool_t *svn_jni__pool;



/*** Code. ***/

/*
 * some local help functions
 */

/*
 * utility function to throw a java exception
 */
static void
svn_jni__throw_exception_by_name(JNIENV *env,
				 const char *name,
				 const char *msg)
{
  jclass cls = NULL;

  /* ensure there is enough memory and stuff
   * for one local reference
   */
  if( (*env)->PushLocalFrame(env, 1) >= 0 )
    {
      jclass cls = (*env)->FindClass(env, name);

      /* if class is null, an exception already has occured */
      if( cls != NULL )
	{
	  (*env)->ThrowNew(env, cls, msg);
	}

      /* pop stack frame */
      (*env)->PopLocalFrame(env, NULL);
    }

  return;
}
   
/*
 * utility function to convert a java string
 * to a native string in the current locale
 */   
static svn_string_t *
svn_jni__jstring_to_native_string(JNIEnv *env, 
				  jstring jstr, 
				  jboolean *hasException,
				  apr_pool_t *pool)
{
  svn_string_t *result = NULL;
  jboolean _hasException = JNI_FALSE;
  
  /* make sure there is enough memory left for 
   * the operation, also push the stack frame
   * we will need 2 local references:
   * - 
   */
  if( (*env)->PushLocalFrame(env, 2) >= 0)
    {
      jbyteArray bytes = NULL;

      bytes = (*env)->CallObjectMethod(env, jstr, "getBytes");
      /* check for exception */
      _hasException = (*env)->ExceptionCheck(env);

      if( !_hasException )
	{
	  char *buffer;
	  jint len = (*env)->GetArrayLength(env, bytes);
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
	      (*env)GetByteArrayRegion(env, bytes, 0, len, 
				       (jbyte *)buffer);
	      buffer[len] = 0;

	      /* now create the svn buffer out of the
	       * buffer... */

	      result = svn_string_create(buffer, pool);

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

/*
 * utility function to create a java hashtable
 * 
 * remark: return hashtable is created as local reference
 */
static jobject
svn_jni__create_hashtable(JNIEnv *env, jboolean *hasException)
{
  jobject hashtable = NULL;
  jboolean _hasException = JNI_FALSE;
  
  /* is there enough memory to have twoadditional
   * local references? 
   * - class reference
   * - constructor method id
   */
  if( (*env)->PushLocalFrame(env, 3) >= 0 )
    {
      jclass hashtableClass = (*env)->FindClass(env,
						"java/util/Hashtable");
      jmethodID hashtableConstructor = NULL;
      
      if( hastableClass == NULL )
	{
	  _hasException = JNI_TRUE;
	}
      else
	{
	  jmethodid hashtableConstructor = 
	    (*env)->GetMethodID(env, hashtableClass,
				"<init>", "()V");
	}

      if( jmethodid == NULL )
	{
	  _hasException = JNI_TRUE;
	}
      else
	{
	  hashtable = (*env)->NewObject(env, hashtableClass,
					hashtableConstructor);
	}

      if( hashtable == NULL )
	{
	  _hasException = JNI_TRUE;
	}

      /* pop local frame but preserve the newly create hashtable */
      (*env)->PopLocalFrame(env, hashtable);
    }

  /* return wether an exception has occured */
  if( hasException != NULL )
    {
      (*hasException) = _hasException;
    }

  return hashtable;
}

/*
 * utility function to add an object to hashtable
 */
static void
svn_jni_hashtable_put(JNIEnv *env, jobject hashtable, jobject key,
		      jobject value, jboolean *hasException)
{
  jboolean _hasException = FALSE;

  /* enough space for two local references?
   * - class reference
   * - method id
   */
  if( (*env)->PushLocalFrame(env, 2) >= 0 )
    {
      jclass hashtableClass = NULL;
      jmethodid hashtablePut = NULL;

      hashtableClass = (*env)->FindClass(env, "java/util/Hashtable");

      if( hashtableClass == NULL )
	{
	  _hasException = TRUE;
	}
      else
	{
	  hashtablePut = 
	    (*env)->GetMethodID(env, hashtableClass, 
				"put", SVN_JNI__HASHTABLE_PUT);
	  if( hashtablePut == NULL )
	    {
	      _hasException = TRUE;
	    }
	}

      if( hashtablePut != NULL )
	{
	  /* the put method usually returns an object
	   * but we dont care about this so we dont have
	   * to take care for the otherweise created
	   * local reference 
	   */
	  (*env)->CallVoidMethod(env, hashtable, hashtablePut,
				   key, value);
	  _hasException = (*env)->ExceptionCheck(env);
	}

      /* pop local references but preserve result */
      (*env)->PopLocalFrame(env, result);
    }

  /* check wether an exception has occured */
  if( hasException != NULL )
    {
      (*hasException) = _hasException;
    }
} 

static auth_baton_t *
svn_jni__make_auth_baton(JNIEnv *env, jobject jobj)
{
  /* the code here will build the auth_baton structure
   * right now, this doesnt work. now only NULL
   * is being returned 
   */

  return NULL;
} 

/*
 * JNI OnLoad Handler
 */
JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM *jvm, void *reserved)
{
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
  char *c_path = svn_jni__GetStringNativeChars(env, path);
  char *c_recursive = "";

  if( recursive == JNI_TRUE )
    {
      c_recursive = " -r ";
    }
 
  printf("command: svn add%s%s\n", c_recursive, c_path);
  printf("doing nothing yet!\n");

  if( c_path != NULL )
    {
      free(c_path);
    }
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

  /* do all the type conversion stuff */
  svn_string_t *target = svn_jni__make_native_string(env, 
						     jtarget, 
						     svn_jni__pool);
  svn_boolean_t descend = jdescend == JNI_TRUE;
  svn_boolean_t get_all = jget_all == JNI_TRUE;
  svn_boolean_t update = jupdate == JNI_TRUE;
  apr_hash_t *statushash;
  svn_auth_baton_t *auth_baton = svn_jni__make_auth_baton(env, jobj);

  if( svn_client_status(&status_hash, target, auth_baton,
                        descend, get_all, update) < 0 )
    {
      /* in the case of an error, throw a java exception */
      svn_jni__throw_exception_by_name("org/tigris/subversion/SubversionException");
    }
  else
    {
      if( (*env)->PushLocalFrame(env, 1) >= 0 )
	{
	  jboolean hasException = JNI_FALSE;
	  jobject hashtable = svn_jni__create_hashtable(env, &hasException);

	  /* now we do have a fresh new hashtable */
	  if( !hasException )
	    {
	      /* iterate through apr hashtable and
	       * insert each item into java hashtable */
	      apr_hash_index_t *index = apr_hash_first(svn_jni__pool, 
						       statushash);
	      while( (index != NULL) && (!_hasException )
		{
		  svn_item_t *item;
		  char *path;
		  svn_wc_status_t *status;
		  jobject jitem;
		  jobject jpath;
		  jobject jstatus;

		  apr_hash_this(hi, NULL, NULL, &item);
		  path = item->key;
		  status = item->value;

		  /* convert native string to java string */
		  jpath = (*env)->NewStringUTF(env, path);
		  if( jpath == NULL )
		    {
		      _hasException = TRUE;
		    }

		  

		  /* now convert to the corresponding
		   * java class
		   */
		  if( !_hasException )
		    {
		      jitem = svn_jni__create_item(env, jkey, jdata, 
						   &_hasException,
						   svn_jni__pool);
		    }

		  /* put entry into java hashtable */
		  if( !_hasException )
		    {
		      svn_jni__hashtable_put(env, hashtable, jkey, jitem, 
					     &hasException);
		    }
		  
		  if( !_hasException )
		    {
		      /* proceed to the next iteration */
		      apr_hash_next(index);
		    }
		}
	    }

	  (*env)->PopLocalFrame(env, hashtable);
	}
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

/* local variables:
 * eval: (load-file "../../../svn-dev.el")
 * end: */
