/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003 CollabNet.  All rights reserved.
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
 * @endcopyright
 */
// SVNClient.cpp: implementation of the SVNClient class.
//
//////////////////////////////////////////////////////////////////////

#include "SVNClient.h"
#include "JNIUtil.h"
#include "Notify.h"
#include "Prompter.h"
#include "Pool.h"
#include "Targets.h"
#include "Revision.h"
#include "JNIByteArray.h"
#include <svn_client.h>
#include <svn_sorts.h>
#include <svn_time.h>
#include <svn_config.h>
#include <svn_io.h>
#include <svn_path.h>
#include "org_tigris_subversion_javahl_Status_Kind.h"
#include "org_tigris_subversion_javahl_Revision.h"
#include <vector>
#include <iostream>
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
struct log_msg_baton
{
  const char *message;
  const char *base_dir;
};

SVNClient::SVNClient()
{
	m_notify = NULL;
	m_prompter = NULL;
}

SVNClient::~SVNClient()
{
	delete m_notify;
	delete m_prompter;
}

jlong SVNClient::getCppAddr()
{
	return reinterpret_cast<jlong>(this);
}

SVNClient * SVNClient::getCppObject(jobject jthis)
{
	static jfieldID fid = 0;
	JNIEnv *env = JNIUtil::getEnv();
	if(fid == 0)
	{
		jclass clazz = env->FindClass(JAVA_PACKAGE"/SVNClient");
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		fid = env->GetFieldID(clazz, "cppAddr", "J");
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
	}

	jlong cppAddr = env->GetLongField(jthis, fid);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	return reinterpret_cast<SVNClient*>(cppAddr);

}

void SVNClient::dispose(jobject jthis)
{
	delete this;
	static jfieldID fid = 0;
	JNIEnv *env = JNIUtil::getEnv();
	if(fid == 0)
	{
		jclass clazz = env->FindClass(JAVA_PACKAGE"/SVNClient");
		if(JNIUtil::isJavaExceptionThrown())
		{
			return;
		}
		fid = env->GetFieldID(clazz, "cppAddr", "J");
		if(JNIUtil::isJavaExceptionThrown())
		{
			return;
		}
	}

	env->SetLongField(jthis, fid, 0);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return;
	}
}

void SVNClient::finalize()
{
	JNIUtil::putFinalizedClient(this);
}

const char * SVNClient::getLastPath()
{
	return m_lastPath.c_str();
}

jobjectArray SVNClient::list(const char *url, Revision &revision, bool recurse)
{
	Pool subPool;
	svn_client_ctx_t *ctx = getContext(NULL);
	if(ctx == NULL)
	{
		return NULL;
	}

	apr_hash_t *dirents;
	svn_error_t *Err = svn_client_ls (&dirents, url, (struct svn_opt_revision_t *)revision.revision (),
                              recurse, ctx, subPool.pool());
	if (Err == NULL)
	{
		apr_array_header_t *array =
		 apr_hash_sorted_keys (dirents, svn_sort_compare_items_as_paths,
							   subPool.pool());
		
		// create the array of DirEntry
		JNIEnv *env = JNIUtil::getEnv();
		jclass clazz = env->FindClass(JAVA_PACKAGE"/DirEntry");
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		jobjectArray ret = env->NewObjectArray(array->nelts, clazz, NULL);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		env->DeleteLocalRef(clazz);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}

		for (int i = 0; i < array->nelts; i++)
		{
			const svn_item_t *item;
			svn_dirent_t *dirent = NULL;

			item = &APR_ARRAY_IDX (array, i, const svn_item_t);
			dirent = (svn_dirent_t *) item->value;

			jobject obj = createJavaDirEntry((const char *)item->key, dirent);
			env->SetObjectArrayElement(ret, i, obj);
			if(JNIUtil::isJavaExceptionThrown())
			{
				return NULL;
			}
			env->DeleteLocalRef(obj);
			if(JNIUtil::isJavaExceptionThrown())
			{
				return NULL;
			}
		}

		return ret;


	}
	else
	{
		JNIUtil::handleSVNError(Err, NULL);
		return NULL;
	}
}

jobjectArray SVNClient::status(const char *path, bool descend, bool onServer)
{
    Pool subPool;
    apr_hash_t *status_hash;
//	svn_client_auth_baton_t *auth_baton = NULL;
	svn_revnum_t youngest = SVN_INVALID_REVNUM;

	svn_client_ctx_t *ctx = getContext(NULL);
	if(ctx == NULL)
	{
		return NULL;
	}

    svn_error_t *Err = svn_client_status (&status_hash, &youngest, path, descend, TRUE,
							 onServer ? TRUE : FALSE,     //update
                             FALSE,     //no_ignore,
							 ctx,
                             subPool.pool());
    if (Err == NULL)
    {
		apr_array_header_t *statusarray =
		 apr_hash_sorted_keys (status_hash, svn_sort_compare_items_as_paths,
							   subPool.pool());
		int i;

		JNIEnv *env = JNIUtil::getEnv();
		jclass clazz = env->FindClass(JAVA_PACKAGE"/Status");
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		jobjectArray ret = env->NewObjectArray(statusarray->nelts, clazz, NULL);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		env->DeleteLocalRef(clazz);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		for (i = 0; i < statusarray->nelts; i++)
		{
			const svn_item_t *item;
			svn_wc_status_t *status = NULL;

			item = &APR_ARRAY_IDX (statusarray, i, const svn_item_t);
			status = (svn_wc_status_t *) item->value;

			jobject obj = createJavaStatus((const char *)item->key, status);
			env->SetObjectArrayElement(ret, i, obj);
			if(JNIUtil::isJavaExceptionThrown())
			{
				return NULL;
			}
			env->DeleteLocalRef(obj);
			if(JNIUtil::isJavaExceptionThrown())
			{
				return NULL;
			}
		}

		return ret;
	}
	else
	{
		JNIUtil::handleSVNError(Err, NULL);
		return NULL;
	}
}

jobject SVNClient::singleStatus(const char *path, bool onServer)
{
	//JNIUtil::logMessage("entering singleStatus");
	//JNIUtil::logMessage(path);
    Pool subPool;
    apr_hash_t *status_hash;
	svn_revnum_t youngest = SVN_INVALID_REVNUM;

	svn_client_ctx_t *ctx = getContext(NULL);
	if(ctx == NULL)
	{
		return NULL;
	}

	//JNIUtil::logMessage("after getContext");
    svn_error_t *Err = svn_client_status (&status_hash, &youngest, path, false, TRUE,
							 onServer ? TRUE : FALSE,     //update
                             FALSE,     //no_ignore,
							 ctx,
                             subPool.pool());
	//JNIUtil::logMessage("after svn_client_status");
    if(Err == NULL)
    {
      apr_array_header_t *statusarray =
         apr_hash_sorted_keys (status_hash, svn_sort_compare_items_as_paths,
                               subPool.pool());
      const svn_item_t *item;
      svn_wc_status_t *status = NULL;

      item = &APR_ARRAY_IDX (statusarray, 0, const svn_item_t);
      status = (svn_wc_status_t *) item->value;

	  //JNIUtil::logMessage("before createJavaStatus");
      return createJavaStatus((const char *) item->key, status);
    }
    else
    {
 		JNIUtil::handleSVNError(Err, NULL);
		return NULL;
	}
}

void SVNClient::username(const char *username)
{
	m_userName = username;
}

void SVNClient::password(const char *password)
{
	m_passWord = password;
}

void SVNClient::setPrompt(Prompter *prompter)
{
	delete m_prompter;
	m_prompter = prompter;
}

jobjectArray SVNClient::logMessages(const char *path, Revision &revisionStart, Revision &revisionEnd)
{
	std::vector<jobject> logs;
    Pool pool;
	m_lastPath = path;
    Targets target (m_lastPath.c_str () );
    apr_pool_t *apr_pool = pool.pool ();
	svn_client_ctx_t *ctx = getContext(NULL);
	if(ctx == NULL)
	{
		return NULL;
	}
    svn_error_t *Err = svn_client_log (target.array (pool),
                        revisionStart.revision (),
                        revisionEnd.revision (),
                        0, // not reverse by default
                        1, // strict by default (not showing cp info)
                        messageReceiver, &logs, ctx, apr_pool);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	if(Err == NULL)
	{
		int size = logs.size();

		JNIEnv *env = JNIUtil::getEnv();
		jclass clazz = env->FindClass(JAVA_PACKAGE"/LogMessage");
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		jobjectArray ret = env->NewObjectArray(size, clazz, NULL);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		env->DeleteLocalRef(clazz);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		for(int i = 0; i < size; i++)
		{
			jobject log = logs[i];
			env->SetObjectArrayElement(ret, i, log);
			if(JNIUtil::isJavaExceptionThrown())
			{
				return NULL;
			}
			env->DeleteLocalRef(log);
			if(JNIUtil::isJavaExceptionThrown())
			{
				return NULL;
			}
		}
		return ret;
	}
    else
    {
 		JNIUtil::handleSVNError(Err, NULL);
		return NULL;
	}
}

void SVNClient::checkout(const char *moduleName, const char *destPath, Revision &revision, bool recurse)
{
    Pool subPool;
    apr_pool_t * apr_pool = subPool.pool ();
    m_lastPath = destPath;

    if(m_notify == NULL)
      return;
	svn_client_ctx_t *ctx = getContext(NULL);
	if(ctx == NULL)
	{
		return ;
	}

    svn_error_t *Err = svn_client_checkout (moduleName,
                                 m_lastPath.c_str (),
                                 revision.revision (),
                                 recurse, ctx,
                                 apr_pool);

    if(Err != NULL)
		JNIUtil::handleSVNError(Err, NULL);

}

void SVNClient::notification(Notify *notify)
{
	delete m_notify;
	m_notify = notify;
}

void SVNClient::remove(const char *path, const char *message, bool force)
{
    svn_client_commit_info_t *commit_info = NULL;
    Pool subPool;
    apr_pool_t * apr_pool = subPool.pool ();
    m_lastPath = path;
	svn_client_ctx_t *ctx = getContext(message);
	if(ctx == NULL)
	{
		return;
	}

    svn_error_t *Err = svn_client_delete (&commit_info, m_lastPath.c_str (), force,
								ctx, apr_pool);
    if(Err != NULL)
 		JNIUtil::handleSVNError(Err, NULL);

}

void SVNClient::undo(const char *path, bool recurse)
{
    Pool subPool;
    apr_pool_t * apr_pool = subPool.pool ();
    m_lastPath = path;
   	svn_client_ctx_t *ctx = getContext(NULL);
	if(ctx == NULL)
	{
		return;
	}
	svn_error_t *Err = svn_client_undo (m_lastPath.c_str (), recurse, ctx, apr_pool);

    if(Err != NULL)
 		JNIUtil::handleSVNError(Err, NULL);

}

void SVNClient::add(const char *path, bool recurse)
{
    Pool subPool;
    apr_pool_t * apr_pool = subPool.pool ();

    m_lastPath = path;
	svn_client_ctx_t *ctx = getContext(NULL);
	if(ctx == NULL)
	{
		return;
	}
    svn_error_t *Err = svn_client_add (m_lastPath.c_str (), recurse, ctx, apr_pool);

    if(Err != NULL)
 		JNIUtil::handleSVNError(Err, NULL);


}

void SVNClient::update(const char *path, Revision &revision, bool recurse)
{
    Pool subPool;
    apr_pool_t * apr_pool = subPool.pool ();
    m_lastPath = path;
   	svn_client_ctx_t *ctx = getContext(NULL);
	if(ctx == NULL)
	{
		return;
	}
    svn_error_t *Err = svn_client_update (m_lastPath.c_str (),
                               revision.revision (),
                               recurse,
							   ctx,
                               apr_pool);
    if(Err != NULL)
 		JNIUtil::handleSVNError(Err, NULL);

}

jlong SVNClient::commit(Targets &targets, const char *message, bool recurse)
{
    Pool subPool;
    apr_pool_t * apr_pool = subPool.pool ();
    svn_client_commit_info_t *commit_info = NULL;
    //m_lastPath = path;

   	svn_client_ctx_t *ctx = getContext(message);
	if(ctx == NULL)
	{
		return -1;
	}
    svn_error_t *Err = svn_client_commit (&commit_info,
							   targets.array (subPool),
                               !recurse, ctx, apr_pool);
    if(Err != NULL)
 		JNIUtil::handleSVNError(Err, NULL);

    if(commit_info && SVN_IS_VALID_REVNUM (commit_info->revision))
      return commit_info->revision;

    return -1;
	return -1;
}

void SVNClient::copy(const char *srcPath, const char *destPath, const char *message, Revision &revision)
{
    Pool subPool;
    apr_pool_t * apr_pool = subPool.pool ();
    Path sourcePath = srcPath;
    m_lastPath = destPath;

    svn_client_commit_info_t *commit_info = NULL;
   	svn_client_ctx_t *ctx = getContext(message);
	if(ctx == NULL)
	{
		return;
	}

    svn_error_t *Err = svn_client_copy (&commit_info,
                             sourcePath.c_str (),
                             revision.revision(),
                             m_lastPath.c_str (),
                             NULL,
							 ctx,
                             apr_pool);
    if(Err != NULL)
 		JNIUtil::handleSVNError(Err, NULL);

}

void SVNClient::move(const char *srcPath, const char *destPath, const char *message, Revision &revision, bool force)
{
    Pool subPool;
    apr_pool_t * apr_pool = subPool.pool ();
    svn_client_commit_info_t *commit_info = NULL;
    Path sourcePath = srcPath;
    m_lastPath = destPath;
   	svn_client_ctx_t *ctx = getContext(message);
	if(ctx == NULL)
	{
		return;
	}


    svn_error_t *Err = svn_client_move (&commit_info,
                             sourcePath.c_str (),
                             revision.revision (),
                             m_lastPath.c_str (),
                             force,
							 ctx,
                             apr_pool);
    if(Err != NULL)
 		JNIUtil::handleSVNError(Err, NULL);
}

void SVNClient::mkdir(const char *path, const char *message)
{
    Pool subPool;
    apr_pool_t * apr_pool = subPool.pool ();
    svn_client_commit_info_t *commit_info = NULL;
    m_lastPath = path;
   	svn_client_ctx_t *ctx = getContext(message);
	if(ctx == NULL)
	{
		return;
	}

    svn_error_t *Err = svn_client_mkdir (&commit_info,
                              path,
							  ctx,
                              apr_pool);

    if(Err != NULL)
 		JNIUtil::handleSVNError(Err, NULL);

}

void SVNClient::cleanup(const char *path)
{
    Pool subPool;
    apr_pool_t * apr_pool = subPool.pool ();
    m_lastPath = path;
   	svn_client_ctx_t *ctx = getContext(NULL);
	if(ctx == NULL)
	{
		return;
	}
    svn_error_t *Err = svn_client_cleanup (m_lastPath.c_str (), ctx, apr_pool);

    if(Err != NULL)
 		JNIUtil::handleSVNError(Err, NULL);

}

void SVNClient::resolve(const char *path, bool recurse)
{
    Pool subPool;
    apr_pool_t * apr_pool = subPool.pool ();
    m_lastPath = path;
   	svn_client_ctx_t *ctx = getContext(NULL);
	if(ctx == NULL)
	{
		return;
	}
    svn_error_t *Err = svn_client_resolve (m_lastPath.c_str (),
                                recurse,
							    ctx,
                                apr_pool);

    if(Err != NULL)
 		JNIUtil::handleSVNError(Err, NULL);

}

void SVNClient::doExport(const char *srcPath, const char *destPath, Revision &revision)
{
    Pool subPool;
    apr_pool_t * apr_pool = subPool.pool ();
    Path sourcePath = srcPath;
    m_lastPath = destPath;
   	svn_client_ctx_t *ctx = getContext(NULL);
	if(ctx == NULL)
	{
		return;
	}
    svn_error_t *Err = svn_client_export (sourcePath.c_str (),
                               m_lastPath.c_str (),
                               const_cast<svn_opt_revision_t*>(
                                 revision.revision ()),
							   ctx,
                               apr_pool);

    if(Err != NULL)
 		JNIUtil::handleSVNError(Err, NULL);


}

void SVNClient::doSwitch(const char *path, const char *url, Revision &revision, bool recurse)
{
    Pool subPool;
    apr_pool_t * apr_pool = subPool.pool ();
    m_lastPath = path;

   	svn_client_ctx_t *ctx = getContext(NULL);
	if(ctx == NULL)
	{
		return;
	}
    svn_error_t *Err = svn_client_switch (m_lastPath.c_str (),
                               url,
                               revision.revision (),
                               recurse,
							   ctx,
                               apr_pool);

    if(Err != NULL)
 		JNIUtil::handleSVNError(Err, NULL);
}

void SVNClient::doImport(const char *path, const char *url, const char *newEntry, const char *message, bool recurse)
{
    Pool subPool;
    apr_pool_t * apr_pool = subPool.pool ();
    m_lastPath = path;
    svn_client_commit_info_t *commit_info = NULL;
   	svn_client_ctx_t *ctx = getContext(message);
	if(ctx == NULL)
	{
		return;
	}

    svn_error_t *Err = svn_client_import (&commit_info,
                               m_lastPath.c_str (),
                               url,
                               newEntry,
                               !recurse,
							   ctx,
                               apr_pool);

    if(Err != NULL)
 		JNIUtil::handleSVNError(Err, NULL);

}

void SVNClient::merge(const char *path1, Revision &revision1, const char *path2, Revision &revision2, const char *localPath, bool force, bool recurse)
{
    Pool subPool;
    apr_pool_t * apr_pool = subPool.pool ();
    m_lastPath = localPath;
    Path srcPath1 = path1;
    Path srcPath2 = path2;
   	svn_client_ctx_t *ctx = getContext(NULL);
	if(ctx == NULL)
	{
		return;
	}

    svn_error_t *Err = svn_client_merge (srcPath1.c_str (),
                              revision1.revision (),
                              srcPath2.c_str (),
                              revision2.revision (),
                              localPath,
                              recurse,
                              force,
                              FALSE,
							  FALSE,
						      ctx,
                              apr_pool);

    if(Err != NULL)
 		JNIUtil::handleSVNError(Err, NULL);

}

jobjectArray SVNClient::properties(jobject jthis, const char *path)
{
  apr_array_header_t * props;
  Pool subPool;
  apr_pool_t * apr_pool = subPool.pool ();
  m_lastPath = path;
	
  Revision rev(Revision::START);
  svn_client_ctx_t *ctx = getContext(NULL);
  if(ctx == NULL)
  {
	return NULL;
  }


  svn_error_t *Err = svn_client_proplist (&props,
                               m_lastPath.c_str (),
                               rev.revision(), 
							   false,
							   ctx,
                               apr_pool);
  if(Err != NULL)
  {
 	JNIUtil::handleSVNError(Err, NULL);
    return NULL;
  }

  // since we disabled recurse, props->nelts should be 1
  for (int j = 0; j < props->nelts; ++j)
  {
    svn_client_proplist_item_t *item =
          ((svn_client_proplist_item_t **)props->elts)[j];

    apr_hash_index_t *hi;

	int count = apr_hash_count (item->prop_hash);

	JNIEnv *env = JNIUtil::getEnv();
	jclass clazz = env->FindClass(JAVA_PACKAGE"/PropertyData");
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	jobjectArray ret = env->NewObjectArray(count, clazz, NULL);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	env->DeleteLocalRef(clazz);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}

	int i = 0;
    for (hi = apr_hash_first (apr_pool, item->prop_hash); hi;
         hi = apr_hash_next (hi), i++)
    {
      const char *key;
      svn_string_t *val;

      apr_hash_this (hi, (const void **)&key, NULL, (void**)&val);

	  jobject object = createJavaProperty(jthis, item->node_name->data, key, val);

	    env->SetObjectArrayElement(ret, i, object);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		env->DeleteLocalRef(object);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
    }
	return ret;
  }
  return NULL;
}

void SVNClient::propertySet(const char *path, const char *name, const char *value, bool recurse)
{
	Pool pool;
	svn_string_t *val = svn_string_create(value, pool.pool());
	propertySet(path, name, val, recurse);
}

void SVNClient::propertySet(const char *path, const char *name, JNIByteArray &value, bool recurse)
{
	Pool pool;
	svn_string_t *val = svn_string_ncreate((const char *)value.getBytes(), value.getLength(), pool.pool());
	propertySet(path, name, val, recurse);
}

void SVNClient::propertyRemove(const char *path, const char *name, bool recurse)
{
	Pool pool;
	propertySet(path, name, (svn_string_t*)NULL, recurse);
}

void SVNClient::propertyCreate(const char *path, const char *name, const char *value, bool recurse)
{
	Pool pool;
	svn_string_t *val = svn_string_create(value, pool.pool());
	propertySet(path, name, val, recurse);
}

void SVNClient::propertyCreate(const char *path, const char *name, JNIByteArray &value, bool recurse)
{
	Pool pool;
	svn_string_t *val = svn_string_ncreate((const char *)value.getBytes(), value.getLength(), pool.pool());
	propertySet(path, name, val, recurse);
}

svn_client_ctx_t * SVNClient::getContext(const char *message)
{
	apr_pool_t *pool = JNIUtil::getRequestPool()->pool();
    svn_auth_baton_t *ab;
	svn_client_ctx_t *ctx = (svn_client_ctx_t *) apr_pcalloc (JNIUtil::getRequestPool()->pool(), sizeof (*ctx));

    apr_array_header_t *providers
      = apr_array_make (pool, 1, sizeof (svn_auth_provider_object_t *));

    /* Fetch our two existing authentication providers, and order them
       in an array. */
    svn_auth_provider_object_t *simple_wc_provider; 

    svn_auth_provider_object_t *username_wc_provider;

    svn_client_get_simple_provider (&simple_wc_provider, pool);
    *(svn_auth_provider_object_t **)apr_array_push (providers) 
      = simple_wc_provider;

    svn_client_get_username_provider (&username_wc_provider, pool);
    *(svn_auth_provider_object_t **)apr_array_push (providers) 
      = username_wc_provider;

    *(svn_auth_provider_object_t **)apr_array_push (providers) = simple_wc_provider;
    *(svn_auth_provider_object_t **)apr_array_push (providers) = username_wc_provider;

    svn_auth_provider_object_t *ssl_server_file_provider;

    svn_auth_provider_object_t *ssl_client_cred_file_provider;

    svn_auth_provider_object_t *ssl_client_pw_file_provider;

    /* The server-cert, client-cert, and client-cert-password  providers. */
    svn_client_get_ssl_server_file_provider(&ssl_server_file_provider, pool);
    *(svn_auth_provider_object_t **)apr_array_push (providers)
      = ssl_server_file_provider;

    svn_client_get_ssl_client_file_provider(&ssl_client_cred_file_provider, pool);
    *(svn_auth_provider_object_t **)apr_array_push (providers)
      = ssl_client_cred_file_provider;

    svn_client_get_ssl_pw_file_provider(&ssl_client_pw_file_provider, pool);
    *(svn_auth_provider_object_t **)apr_array_push (providers)
      = ssl_client_pw_file_provider;

	svn_auth_provider_object_t *prompt_provider = Prompter::getProvider(m_prompter);
    *(svn_auth_provider_object_t **)apr_array_push (providers) = prompt_provider;

	svn_auth_provider_object_t *prompt_provider_server_ssl = Prompter::getProviderServerSSL(m_prompter);
    *(svn_auth_provider_object_t **)apr_array_push (providers) = prompt_provider_server_ssl;
	
	svn_auth_provider_object_t *prompt_provider_client_ssl = Prompter::getProviderClientSSL(m_prompter);
    *(svn_auth_provider_object_t **)apr_array_push (providers) = prompt_provider_client_ssl;

	svn_auth_provider_object_t *prompt_provider_client_ssl_pass = Prompter::getProviderClientSSLPass(m_prompter);
    *(svn_auth_provider_object_t **)apr_array_push (providers) = prompt_provider_client_ssl_pass;

    /* Build an authentication baton to give to libsvn_client. */
    svn_auth_open (&ab, providers, pool);

    /* Place any default --username or --password credentials into the
       auth_baton's run-time parameter hash.  ### Same with --no-auth-cache? */
    if (!m_userName.empty())
      svn_auth_set_parameter(ab, SVN_AUTH_PARAM_DEFAULT_USERNAME,
                             m_userName.c_str());
    if (!m_passWord.empty())
      svn_auth_set_parameter(ab, SVN_AUTH_PARAM_DEFAULT_PASSWORD,
                             m_passWord.c_str());

	ctx->auth_baton = ab;
	ctx->prompt_func = NULL;
	ctx->prompt_baton = NULL;
	ctx->notify_func = Notify::notify;
	ctx->notify_baton = m_notify;
	ctx->log_msg_func = getCommitMessage;
	ctx->log_msg_baton = getCommitMessageBaton(message);
	svn_error_t *err = NULL;
    if (( err = svn_config_get_config (&(ctx->config), JNIUtil::getRequestPool()->pool())))
    {
		JNIUtil::handleSVNError(err, NULL);
        return NULL;
    }

	return ctx;
}
svn_error_t *SVNClient::getCommitMessage(const char **log_msg, const char **tmp_file,
                                apr_array_header_t *commit_items, void *baton,
                                apr_pool_t *pool)
{
	*log_msg = NULL;
	*tmp_file = NULL;
	log_msg_baton *lmb = (log_msg_baton *) baton;

	if (lmb && lmb->message)
	{
		*log_msg = apr_pstrdup (pool, lmb->message);
		return SVN_NO_ERROR;
	}

	return SVN_NO_ERROR;
}
void *SVNClient::getCommitMessageBaton(const char *message, const char *baseDir)
{
	if(message != NULL)
	{
		log_msg_baton *baton = (log_msg_baton *)
			apr_palloc (JNIUtil::getRequestPool()->pool(), sizeof (*baton));

		baton->message = message;
		baton->base_dir = baseDir ? baseDir : ".";

		return baton;
	}
	return NULL;
}
jobject SVNClient::createJavaStatus(const char *path, svn_wc_status_t *status)
{
	JNIEnv *env = JNIUtil::getEnv();
	jclass clazz = env->FindClass(JAVA_PACKAGE"/Status");
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	//JNIUtil::logMessage("after FindClass");
	static jmethodID mid = 0;
	if(mid == 0)
	{
		mid = env->GetMethodID(clazz, "<init>", "(Ljava/lang/String;ZJJLjava/lang/String;IIZZZIILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;J)V");
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		//JNIUtil::logMessage("after MethodID");
	}
	jstring jPath = JNIUtil::makeJString(path);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	//JNIUtil::logMessage("after makeJString(path)");

	jboolean jIsVersioned = JNI_FALSE;
	jboolean jIsDir = JNI_FALSE;
	jboolean jIsCopied = JNI_FALSE;
	jboolean jIsLocked = JNI_FALSE;
	jlong jRevision = org_tigris_subversion_javahl_Revision_SVN_INVALID_REVNUM;
	jlong jLastChanged = 0;
	jstring jLastCommitAuthor = NULL;
	jint jTextType = org_tigris_subversion_javahl_Status_Kind_none;
	jint jPropType = org_tigris_subversion_javahl_Status_Kind_none;
	jint jRepositoryTextType = org_tigris_subversion_javahl_Status_Kind_none;
	jint jRepositoryPropType = org_tigris_subversion_javahl_Status_Kind_none;
	jstring jConflictNew = NULL;
	jstring jConflictOld = NULL;
	jstring jConflictWorking = NULL;
	jstring jURL = NULL;
	jstring jURLCopiedFrom = NULL;
	jlong jRevisionCopiedFrom = org_tigris_subversion_javahl_Revision_SVN_INVALID_REVNUM;

	if(status != NULL)
	{
		jIsVersioned = (status->text_status > svn_wc_status_unversioned) ? JNI_TRUE : JNI_FALSE;
		jTextType = mapStatusKind(status->text_status);
		jPropType = mapStatusKind(status->prop_status);
		jRepositoryTextType = mapStatusKind(status->repos_text_status);
		jRepositoryPropType = mapStatusKind(status->repos_prop_status);
		jIsCopied = (status->copied == 1) ? JNI_TRUE: JNI_FALSE;
		jIsLocked = (status->locked == 1) ? JNI_TRUE: JNI_FALSE;

	    svn_wc_entry_t * entry = status->entry;
		if (entry != NULL)
		{
			jRevision = entry->revision;
			jLastChanged = entry->cmt_rev;
			jLastCommitAuthor = JNIUtil::makeJString(entry->cmt_author);
			if(JNIUtil::isJavaExceptionThrown())
			{
				return NULL;
			}
			//JNIUtil::logMessage("after makeJString(entry->cmt_author)");
			jIsDir = (entry->kind == svn_node_dir) ? JNI_TRUE: JNI_FALSE;
			jConflictNew = JNIUtil::makeJString(entry->conflict_new);
			if(JNIUtil::isJavaExceptionThrown())
			{
				return NULL;
			}
			//JNIUtil::logMessage("after makeJString(entry->conflict_new)");
			jConflictOld = JNIUtil::makeJString(entry->conflict_old);
			if(JNIUtil::isJavaExceptionThrown())
			{
				return NULL;
			}
			//JNIUtil::logMessage("after makeJString(entry->conflict_old)");
			jConflictWorking= JNIUtil::makeJString(entry->conflict_wrk);
			if(JNIUtil::isJavaExceptionThrown())
			{
				return NULL;
			}
			//JNIUtil::logMessage("after makeJString(entry->conflict_work)");
			jURL = JNIUtil::makeJString(entry->url);
			if(JNIUtil::isJavaExceptionThrown())
			{
				return NULL;
			}
			//JNIUtil::logMessage("after makeJString(entry->conflict_work)");
			jURLCopiedFrom = JNIUtil::makeJString(entry->copyfrom_url);
			if(JNIUtil::isJavaExceptionThrown())
			{
				return NULL;
			}
			//JNIUtil::logMessage("after makeJString(entry->copyfrom_url)");
			jRevisionCopiedFrom = entry->copyfrom_rev;
		}
	}

	jobject ret = env->NewObject(clazz, mid, jPath, jIsDir, jRevision, jLastChanged, jLastCommitAuthor,
		jTextType, jPropType, jIsVersioned, jIsLocked, jIsCopied, jRepositoryTextType, jRepositoryPropType,
		jConflictOld, jConflictNew, jConflictWorking, jURL, jURLCopiedFrom, jRevisionCopiedFrom);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	//JNIUtil::logMessage("NewObject");
	env->DeleteLocalRef(clazz);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	//JNIUtil::logMessage("DeleteLocalRef(clazz)");
	env->DeleteLocalRef(jPath);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	//JNIUtil::logMessage("DeleteLocalRef(jPath)");
	if(jLastCommitAuthor != NULL)
	{
		env->DeleteLocalRef(jLastCommitAuthor);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		//JNIUtil::logMessage("DeleteLocalRef(jLastCommitAuthor)");
	}
	if(jConflictNew != NULL)
	{
		env->DeleteLocalRef(jConflictNew);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		//JNIUtil::logMessage("DeleteLocalRef(jConflictNew)");
	}
	if(jConflictOld != NULL)
	{
		env->DeleteLocalRef(jConflictOld);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		//JNIUtil::logMessage("DeleteLocalRef(jConflictOld)");
	}
	if(jConflictWorking != NULL)
	{
		env->DeleteLocalRef(jConflictWorking);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		//JNIUtil::logMessage("DeleteLocalRef(jConflictWorking");
	}
	if(jURL != NULL)
	{
		env->DeleteLocalRef(jURL);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		//JNIUtil::logMessage("DeleteLocalRef(jURL");
	}
	if(jURLCopiedFrom != NULL)
	{
		env->DeleteLocalRef(jURLCopiedFrom);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		//JNIUtil::logMessage("DeleteLocalRef(jURLCopiedFrom");
	}
	return ret;
}
jint SVNClient::mapStatusKind(int svnKind)
{
	switch(svnKind)
	{
    case svn_wc_status_none:
	default:
		return org_tigris_subversion_javahl_Status_Kind_none;
    case svn_wc_status_unversioned:
		return org_tigris_subversion_javahl_Status_Kind_unversioned;
    case svn_wc_status_normal:
		return org_tigris_subversion_javahl_Status_Kind_normal;
    case svn_wc_status_added:
		return org_tigris_subversion_javahl_Status_Kind_added;
    case svn_wc_status_absent:
		return org_tigris_subversion_javahl_Status_Kind_absent;
    case svn_wc_status_deleted:
		return org_tigris_subversion_javahl_Status_Kind_deleted;
    case svn_wc_status_replaced:
		return org_tigris_subversion_javahl_Status_Kind_replaced;
    case svn_wc_status_modified:
		return org_tigris_subversion_javahl_Status_Kind_modified;
    case svn_wc_status_merged:
		return org_tigris_subversion_javahl_Status_Kind_merged;
    case svn_wc_status_conflicted:
		return org_tigris_subversion_javahl_Status_Kind_conflicted;
    case svn_wc_status_obstructed:
		return org_tigris_subversion_javahl_Status_Kind_obstructed;
	}
}
svn_error_t *SVNClient::messageReceiver (void *baton, apr_hash_t * changed_paths,
                 svn_revnum_t rev, const char *author, const char *date,
                 const char *msg, apr_pool_t * pool)
{
	if(JNIUtil::isJavaExceptionThrown())
	{
		return SVN_NO_ERROR;
	}
    svn_error_t * error = NULL;
	std::vector<jobject> *logs = (std::vector<jobject>*)baton;

    apr_time_t timeTemp;
    svn_time_from_cstring (&timeTemp, date, pool);

	static jmethodID mid = 0;
	JNIEnv *env = JNIUtil::getEnv();
	jclass clazz = env->FindClass(JAVA_PACKAGE"/LogMessage");
	if(JNIUtil::isJavaExceptionThrown())
	{
		return SVN_NO_ERROR;
	}

	if(mid == 0)
	{
		mid = env->GetMethodID(clazz, "<init>", "(Ljava/lang/String;Ljava/util/Date;JLjava/lang/String;)V");
		if(JNIUtil::isJavaExceptionThrown() || mid == 0)
		{
			return SVN_NO_ERROR;
		}
	}

	jstring jmessage = JNIUtil::makeJString(msg);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return SVN_NO_ERROR;
	}

	jobject jdate = JNIUtil::createDate(timeTemp);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return SVN_NO_ERROR;
	}

	jstring jauthor = JNIUtil::makeJString(author);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return SVN_NO_ERROR;
	}

	jobject log = env->NewObject(clazz, mid, jmessage, jdate, (jlong)rev, jauthor);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return SVN_NO_ERROR;
	}
	logs->push_back(log);
	env->DeleteLocalRef(clazz);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return SVN_NO_ERROR;
	}
	env->DeleteLocalRef(jmessage);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return SVN_NO_ERROR;
	}
	env->DeleteLocalRef(jdate);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return SVN_NO_ERROR;
	}
	env->DeleteLocalRef(jauthor);
	return SVN_NO_ERROR;
}

jobject SVNClient::createJavaProperty(jobject jthis, const char *path, const char *name, svn_string_t *value)
{
	JNIEnv *env = JNIUtil::getEnv();
	jclass clazz = env->FindClass(JAVA_PACKAGE"/PropertyData");
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	static jmethodID mid = 0;
	if(mid == 0)
	{
		mid = env->GetMethodID(clazz, "<init>", "(L"JAVA_PACKAGE"/SVNClient;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;[B)V");
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
	}
	jstring jPath = JNIUtil::makeJString(path);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	jstring jName = JNIUtil::makeJString(name);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	jstring jValue = JNIUtil::makeJString(value->data);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	jbyteArray jData = JNIUtil::makeJByteArray((const signed char *)value->data, value->len);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	jobject ret = env->NewObject(clazz, mid, jthis, jPath, jName, jValue, jData);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	env->DeleteLocalRef(clazz);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	env->DeleteLocalRef(jPath);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	env->DeleteLocalRef(jName);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	env->DeleteLocalRef(jValue);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	env->DeleteLocalRef(jData);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	return ret;
}

void SVNClient::propertySet(const char *path, const char *name, svn_string_t *value, bool recurse)
{
  svn_error_t * error = svn_client_propset (name, value, path,
								recurse, JNIUtil::getRequestPool()->pool());
  if(error != NULL)
 		JNIUtil::handleSVNError(error, NULL);
}

jbyteArray SVNClient::fileContent(const char *path, Revision &revision)
{
	Pool pool;
	svn_stream_t *read_stream = NULL;
	size_t size = 0;

	if(revision.revision()->kind == svn_opt_revision_base) 
	// we want the base of the current working copy. Bad hack to avoid going to the server
	{

		const char *ori_path = svn_path_internal_style(path, pool.pool());
		const char *base_path;
		svn_error_t *err = svn_wc_get_pristine_copy_path (ori_path,
                               &base_path,
                               pool.pool());
		if(err != NULL)
		{
			JNIUtil::handleSVNError(err, NULL);
			return NULL;
		}
		apr_file_t *file = NULL;
		apr_finfo_t finfo;
		apr_status_t apr_err = apr_stat(&finfo, base_path,
                                   APR_FINFO_MIN, pool.pool());
		if(apr_err)
		{
			JNIUtil::handleAPRError(apr_err, "open file");
			return NULL;
		}
		apr_err = apr_file_open(&file, base_path, APR_READ, 0, pool.pool());
		if(apr_err)
		{
			JNIUtil::handleAPRError(apr_err, "open file");
			return NULL;
		}
		read_stream = svn_stream_from_aprfile(file, pool.pool());
		size = finfo.size;
	}	
	else 
	{
		svn_client_ctx_t * ctx = getContext(NULL);
		if(ctx == NULL)
		{
			return NULL;
		}
		svn_stringbuf_t *buf = svn_stringbuf_create("", pool.pool());
		read_stream = svn_stream_from_stringbuf(buf, pool.pool());
		svn_error_t *err = svn_client_cat (read_stream,
                path, revision.revision(), ctx, pool.pool());
		if(err != NULL)
		{
			JNIUtil::handleSVNError(err, NULL);
			return NULL;
		}
		size = buf->len;
	}
	if(read_stream == NULL)
	{
		return NULL;
	}

	JNIEnv *env = JNIUtil::getEnv();
	jbyteArray ret = env->NewByteArray(size);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	jbyte *retdata = env->GetByteArrayElements(ret, NULL);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	svn_error_t *err = svn_stream_read (read_stream, (char *)retdata,
                              &size);

	if(err != NULL)
	{
		env->ReleaseByteArrayElements(ret, retdata, 0);
		JNIUtil::handleSVNError(err, NULL);
		return NULL;
	}
	env->ReleaseByteArrayElements(ret, retdata, 0);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}


	return ret;
}

jobject SVNClient::createJavaDirEntry(const char *path, svn_dirent_t *dirent)
{
	JNIEnv *env = JNIUtil::getEnv();
	jclass clazz = env->FindClass(JAVA_PACKAGE"/DirEntry");
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	static jmethodID mid = 0;
	if(mid == 0)
	{
		// public DirEntry(String path,int nodeKind, long size, boolean hasProps, long lastChangedRevision, long lastChanged, String lastAuthor)
		mid = env->GetMethodID(clazz, "<init>", "(Ljava/lang/String;IJZJJLjava/lang/String;)V");
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
	}
	jstring jPath = JNIUtil::makeJString(path);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	jint jNodeKind = dirent->kind;
	jlong jSize = dirent->size;
	jboolean jHasProps = (dirent->has_props? JNI_TRUE : JNI_FALSE);
	jlong jLastChangedRevision = dirent->created_rev;
	jlong jLastChanged = dirent->time;
	jstring jLastAuthor = JNIUtil::makeJString(dirent->last_author);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	jobject ret = env->NewObject(clazz, mid, jPath, jNodeKind, jSize, jHasProps, jLastChangedRevision, jLastChanged,jLastAuthor);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	env->DeleteLocalRef(clazz);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	env->DeleteLocalRef(jPath);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	if(jLastAuthor != NULL)
	{
		env->DeleteLocalRef(jLastAuthor);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
	}
	return ret;
}

jobject SVNClient::revProperty(jobject jthis, const char *path, const char *name, Revision &rev)
{
  Pool subPool;
  apr_pool_t * apr_pool = subPool.pool ();
  m_lastPath = path;
	
  svn_client_ctx_t *ctx = getContext(NULL);
  if(ctx == NULL)
  {
	return NULL;
  }
  const char *URL;
  svn_string_t *propval;
  svn_revnum_t set_rev;
  svn_error_t * error = svn_client_url_from_path (&URL, path, apr_pool);  

  if(error != SVN_NO_ERROR)
  {
 	JNIUtil::handleSVNError(error, NULL);
	return NULL;
  }
   
  if(URL == NULL)
  {
	  JNIUtil::handleSVNError(svn_error_create(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                                "Either a URL or versioned item is required."), NULL);
	  return NULL;
  }
      
  error = svn_client_revprop_get (name, &propval,
                                       URL, rev.revision(),
                                       &set_rev, ctx, apr_pool);
  if(error != SVN_NO_ERROR)
  {
 	JNIUtil::handleSVNError(error, NULL);
	return NULL;
  }

  return createJavaProperty(jthis, path, name, propval);
}
