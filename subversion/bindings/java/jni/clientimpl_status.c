/*
 * native implementation of the functions declared in
 * the Java class org.tigris.subversion.lib.ClientImpl
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
#include <apr.h>
#include <apr_general.h>
#include <jni.h>
#include <malloc.h>
#include <svn_pools.h>
#include <svn_client.h>
#include <svn_string.h>
#include "entry.h"
#include "global.h"
#include "hashtable.h"
#include "misc.h"
#include "svn_jni.h"
#include "status.h"
#include "string.h"

/*** Defines ***/
#define SVN_JNI__CLIENTIMPL_STATUS \
"Java_org_tigris_subversion_lib_ClientImpl_status"

/*** Code ***/
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
  apr_hash_t *statushash = NULL;
  svn_client_auth_baton_t *auth_baton = NULL;
  svn_error_t *error = NULL;
  apr_pool_t *pool = NULL;
  svn_revnum_t youngest;

#ifdef SVN_JNI__VERBOSE
  fprintf(stderr, ">>>" SVN_JNI__CLIENTIMPL_STATUS "\n");
#endif

  /* create a new pool
   * lifetime is the scope of this function
   */
  pool = svn_pool_create(NULL);

  /* HERE COMES SOME DEBUGGING CODE
     IF YOU DONT WANT TO USE IT, JUST LEAVE
     IT In COMMENTS
  --> 
  {
    int i=0;
    hashtable = hashtable__create(env, &hasException);

    (*env)->PushLocalFrame(env, 4);

    if( !hasException )
      {
        for( i=0; i<1000; i++ )
          {
            char puffer[20];
            jstring jkey;
            jstring jvalue;

            sprintf(puffer,"X%04d", i);
            jkey = string__c_to_j(env, puffer, NULL);
            jvalue = string__c_to_j(env, puffer, NULL);

            hashtable__put(env, hashtable, jkey, jvalue, &hasException);

            (*env)->DeleteLocalRef(env, jkey);
            (*env)->DeleteLocalRef(env, jvalue);

            if( hasException )
              {
                break;
              }
          }
      }
    (*env)->PopLocalFrame(env, 0);
  }

  return hashtable;

  <-- */

  /* do all the type conversion stuff */
  target_string = string__j_to_svn_string(env, 
                                   jtarget, &hasException,
                                   pool);

  if( !hasException )
    {
      target_stringbuf = 
	svn_stringbuf_create_from_string(target_string, 
					 pool);

      if( target_stringbuf == NULL )
	{
	  /* seems like the conversion didnt succeed */
	  hasException = JNI_TRUE;
	  misc__throw_exception_by_name(env, 
                                        SVN_JNI__SUBVERSION_EXCEPTION,
                                        SVN_JNI__ERROR_CREATE_STRINGBUF);
	}

    }

  if( !hasException )
    {
      auth_baton = misc__make_auth_baton(env, jobj);

#ifdef SVN_JNI__VERBOSE
      fprintf(stderr, ">>>svn_client_status(");
      SVN_JNI__DEBUG_PTR(statushash);
      SVN_JNI__DEBUG_PTR(target_stringbuf);
      if( target_stringbuf != NULL )
        {
          SVN_JNI__DEBUG_STR(target_stringbuf->data);
        }
      SVN_JNI__DEBUG_PTR(auth_baton);
      SVN_JNI__DEBUG_BOOL(descend);
      SVN_JNI__DEBUG_BOOL(get_all);
      SVN_JNI__DEBUG_BOOL(update);
      SVN_JNI__DEBUG_PTR(pool);
      fprintf(stderr, ")\n");
#endif
      
      error = svn_client_status(&statushash, &youngest, target_stringbuf, 
                                auth_baton,descend, 
                                get_all, update, 
                                pool);

      /* ### todo: `youngest' is tossed right now, need to propagate
         the change to the Java interface. */

      if( error != NULL )
	{
#ifdef SVN_JNI__VERBOSE
          SVN_JNI__DEBUG_STR(error->message);
#endif
	  /* in the case of an error, throw a java exception */
	  hasException = JNI_TRUE;
	  misc__throw_exception_by_name(env, 
                                        SVN_JNI__SUBVERSION_EXCEPTION,
                                        SVN_JNI__ERROR_CLIENT_STATUS);
	}
#ifdef SVN_JNI__VERBOSE
      fprintf(stderr, "\n<<<svn_client_status\n");
#endif
    }

  if( !hasException )
    {
      hashtable = hashtable__create(env, &hasException);
    }

  if( !hasException )
    {
      /*
       * ensure needed preferences:
       * - hashtable class
      * - hashtable instance
       * = 2 references
       */
      if( (*env)->PushLocalFrame(env, 2) >= 0 )
	{
	  /* now we do have a fresh new hashtable */
	  if( !hasException )
	    {
              apr_hash_index_t *index;
              int i=0;

#ifdef SVN_JNI__VERBOSE
              int statushash_count=apr_hash_count(statushash);
              SVN_JNI__DEBUG_DEC(statushash_count);
              fprintf(stderr, "\n");
#endif

	      /* iterate through apr hashtable and
	       * insert each item into java hashtable */
	      index = apr_hash_first(pool, 
                                     statushash);

#ifdef SVN_JNI__VERBOSE
              fprintf(stderr, "apr_hash_first(");
              SVN_JNI__DEBUG_PTR(pool);
              SVN_JNI__DEBUG_PTR(statushash);
              SVN_JNI__DEBUG_PTR(index);
              fprintf(stderr, ")\n");
              fprintf(stderr, "while loop\n");
#endif

	      while( (index != NULL) && (!hasException ) )
		{
                  const void *key=NULL;
		  char *path;
                  apr_size_t len;
		  svn_wc_status_t *status;
		  //jobject jitem = NULL;
		  jstring jpath = NULL;
		  jobject jstatus = NULL;
                  void *val=NULL;

		  apr_hash_this(index, &key, &len, &val);

#ifdef SVN_JNI__VERBOSE
                  fprintf(stderr, "apr_hash_this(");
                  SVN_JNI__DEBUG_DEC(len);
                  SVN_JNI__DEBUG_STR(key);
                  SVN_JNI__DEBUG_PTR(val);
                  SVN_JNI__DEBUG_BOOL(hasException);
                  fprintf(stderr, ")\n");
#endif
		  //path = item->key;
		  //status = item->data;
 
                  path = (char*)key;
                  status = (svn_wc_status_t*)val;
 
		  /* convert native string to java string */
		  jpath = string__c_to_j(env, path, &hasException);

		  /* convert svn_wc_status_t to java class Status */
		  if( !hasException )
		    {
		      jstatus = status__create(env, status, 
                                               &hasException);
		    }
                  

		  /* put entry into java hashtable */
		  if( !hasException )
		    {
		      hashtable__put(env, hashtable, jpath, jstatus, 
                                     &hasException);

		    }
		  
		  if( !hasException )
                  {
#ifdef SVN_JNI__VERBOSE
                    fprintf(stderr, "apr_hash_next(");
                    SVN_JNI__DEBUG_PTR(index);
                    fprintf(stderr, ")...");
#endif

                    /* proceed to the next iteration */
                    apr_hash_next(index);
#ifdef SVN_JNI__VERBOSE
                    fprintf(stderr, "Done\n");
#endif
                  }

                  /* delete local refs... */
                  if( jpath != NULL )
                    {
#ifdef SVN_JNI__VERBOSE
                      fprintf(stderr, "\nDeleteLocalRef(jpath)...");
#endif
                      (*env)->DeleteLocalRef(env, jpath);
#ifdef SVN_JNI__VERBOSE
                      fprintf(stderr, "Done\n");
#endif
                    }
                  if( jstatus != NULL )
                    {
#ifdef SVN_JNI__VERBOSE
                      fprintf(stderr, "DeleteLocalRef(jstatus)...");
#endif
                      (*env)->DeleteLocalRef(env, jstatus);
#ifdef SVN_JNI__VERBOSE
                      fprintf(stderr, "Done\n");
#endif
                    }
		} /* while( ... ) */

#ifdef SVN_JNI__VERBOSE
              SVN_JNI__DEBUG_BOOL(hasException);
              fprintf(stderr, "\nend of while loop!\n");
#endif

	    } /* if( !_hasException ) */

	  (*env)->PopLocalFrame(env, hashtable);
	} /* if( ... Push ... ) */
    }

  /* destroy the local pool */
  if( pool != NULL )
    {
      svn_pool_destroy(pool);
    }

#ifdef SVN_JNI__VERBOSE
  fprintf(stderr, "\n<<<" SVN_JNI__CLIENTIMPL_STATUS "(");
  SVN_JNI__DEBUG_BOOL(hasException);
  fprintf(stderr, ")\n");
#endif

  return hashtable;
}

/* 
 * local variables:
 * eval: (load-file "../../../svn-dev.el")
 * end: 
 */
