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
// Prompter.cpp: implementation of the Prompter class.
//
//////////////////////////////////////////////////////////////////////

#include "Prompter.h"
#include "Pool.h"
#include "JNIUtil.h"
#include "JNIStringHolder.h"
#include <svn_client.h>
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

Prompter::Prompter(jobject jprompter)
{
	m_prompter = jprompter;
}

Prompter::~Prompter()
{
	if(m_prompter!= NULL)
	{
		JNIEnv *env = JNIUtil::getEnv();
		env->DeleteGlobalRef(m_prompter);
	}
}

Prompter *Prompter::makeCPrompter(jobject jpromper)
{
	if(jpromper == NULL)
	{
		return NULL;
	}
	JNIEnv *env = JNIUtil::getEnv();
	jclass clazz = env->FindClass(JAVA_PACKAGE"/PromptUserPassword");
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	if(!env->IsInstanceOf(jpromper, clazz))
	{
		env->DeleteLocalRef(clazz);
		return NULL;
	}
	env->DeleteLocalRef(clazz);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	jobject myPrompt = env->NewGlobalRef(jpromper);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	return new Prompter(myPrompt);
}

jstring Prompter::username()
{
	static jmethodID mid = 0;
	JNIEnv *env = JNIUtil::getEnv();
	if(mid == 0)
	{
		jclass clazz = env->FindClass(JAVA_PACKAGE"/PromptUserPassword");
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		mid = env->GetMethodID(clazz, "getUsername", "()Ljava/lang/String;");
		if(JNIUtil::isJavaExceptionThrown() || mid == 0)
		{
			return NULL;
		}
		env->DeleteLocalRef(clazz);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return false;
		}
	}

	return  static_cast<jstring>(env->CallObjectMethod(m_prompter, mid));
}

jstring Prompter::password()
{
	static jmethodID mid = 0;
	JNIEnv *env = JNIUtil::getEnv();
	if(mid == 0)
	{
		jclass clazz = env->FindClass(JAVA_PACKAGE"/PromptUserPassword");
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		mid = env->GetMethodID(clazz, "getPassword", "()Ljava/lang/String;");
		if(JNIUtil::isJavaExceptionThrown() || mid == 0)
		{
			return NULL;
		}
		env->DeleteLocalRef(clazz);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return false;
		}
	}

	return  static_cast<jstring>(env->CallObjectMethod(m_prompter, mid));
}
bool Prompter::askYesNo(const char *realm, const char *question, bool yesIsDefault)
{
	static jmethodID mid = 0;
	JNIEnv *env = JNIUtil::getEnv();
	if(mid == 0)
	{
		jclass clazz = env->FindClass(JAVA_PACKAGE"/PromptUserPassword");
		if(JNIUtil::isJavaExceptionThrown())
		{
			return false;
		}
		mid = env->GetMethodID(clazz, "askYesNo", "(Ljava/lang/String;Ljava/lang/String;Z)Z");
		if(JNIUtil::isJavaExceptionThrown() || mid == 0)
		{
			return false;
		}
		env->DeleteLocalRef(clazz);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return false;
		}
	}

	jstring jrealm = JNIUtil::makeJString(realm);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return false;
	}
	jstring jquestion = JNIUtil::makeJString(question);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return false;
	}
	jboolean ret = env->CallBooleanMethod(m_prompter, mid, jrealm, jquestion, yesIsDefault ? JNI_TRUE : JNI_FALSE);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return false;
	}
	env->DeleteLocalRef(jquestion);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return false;
	}
	env->DeleteLocalRef(jrealm);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return false;
	}
	return ret ? true:false;
}
const char *Prompter::askQuestion(const char *realm, const char *question, bool showAnswer)
{
	static jmethodID mid = 0;
	JNIEnv *env = JNIUtil::getEnv();
	if(mid == 0)
	{
		jclass clazz = env->FindClass(JAVA_PACKAGE"/PromptUserPassword");
		if(JNIUtil::isJavaExceptionThrown())
		{
			return false;
		}
		mid = env->GetMethodID(clazz, "askQuestion", "(Ljava/lang/String;Ljava/lang/String;Z)Ljava/lang/String;");
		if(JNIUtil::isJavaExceptionThrown() || mid == 0)
		{
			return false;
		}
		env->DeleteLocalRef(clazz);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return false;
		}
	}

	jstring jrealm = JNIUtil::makeJString(realm);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return false;
	}
	jstring jquestion = JNIUtil::makeJString(question);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return false;
	}
	jstring janswer = static_cast<jstring>(env->CallObjectMethod(m_prompter, mid, jrealm, jquestion, showAnswer ? JNI_TRUE : JNI_FALSE));
	if(JNIUtil::isJavaExceptionThrown())
	{
		return false;
	}
	env->DeleteLocalRef(jquestion);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return false;
	}
	env->DeleteLocalRef(jrealm);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return false;
	}
	JNIStringHolder answer(janswer);
	if(answer != NULL)
	{
		m_answer = answer;
	}
	else
	{
		m_answer = "";
	}
	return m_answer.c_str();
}
bool Prompter::prompt(const char *realm, const char *username)
{
	static jmethodID mid = 0;
	JNIEnv *env = JNIUtil::getEnv();
	if(mid == 0)
	{
		jclass clazz = env->FindClass(JAVA_PACKAGE"/PromptUserPassword");
		if(JNIUtil::isJavaExceptionThrown())
		{
			return false;
		}
		mid = env->GetMethodID(clazz, "prompt", "(Ljava/lang/String;Ljava/lang/String;)Z");
		if(JNIUtil::isJavaExceptionThrown() || mid == 0)
		{
			return false;
		}
		env->DeleteLocalRef(clazz);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return false;
		}
	}

	jstring jrealm = JNIUtil::makeJString(realm);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return false;
	}
	jstring jusername = JNIUtil::makeJString(username);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return false;
	}
	jboolean ret = env->CallBooleanMethod(m_prompter, mid, jrealm, jusername);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return false;
	}
	env->DeleteLocalRef(jusername);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return false;
	}
	env->DeleteLocalRef(jrealm);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return false;
	}
	return ret ? true:false;
}
svn_auth_provider_object_t *Prompter::getProvider(Prompter *that)
{
	apr_pool_t *pool = JNIUtil::getRequestPool()->pool();
    svn_auth_provider_object_t *prompt_provider = (svn_auth_provider_object_t *)apr_pcalloc (pool, sizeof(*prompt_provider));
	svn_auth_provider_t *vtable = (svn_auth_provider_t *)apr_pcalloc (pool, sizeof(svn_auth_provider_t));
	vtable->cred_kind = SVN_AUTH_CRED_SIMPLE;
	vtable->first_credentials = firstCreds;
	vtable->next_credentials = nextCreds;
	vtable->save_credentials = NULL;
	prompt_provider->vtable = vtable;
	prompt_provider->provider_baton = that;
	return prompt_provider;
}
svn_auth_provider_object_t *Prompter::getProviderServerSSL(Prompter *that)
{
	apr_pool_t *pool = JNIUtil::getRequestPool()->pool();
    svn_auth_provider_object_t *prompt_provider = (svn_auth_provider_object_t *)apr_pcalloc (pool, sizeof(*prompt_provider));
	svn_auth_provider_t *vtable = (svn_auth_provider_t *)apr_pcalloc (pool, sizeof(svn_auth_provider_t));
	vtable->cred_kind = SVN_AUTH_CRED_SERVER_SSL;
	vtable->first_credentials = firstCreds_server_ssl;
	vtable->next_credentials = NULL;
	vtable->save_credentials = NULL;
	prompt_provider->vtable = vtable;
	prompt_provider->provider_baton = that;
	return prompt_provider;
}
svn_auth_provider_object_t *Prompter::getProviderClientSSL(Prompter *that)
{
	apr_pool_t *pool = JNIUtil::getRequestPool()->pool();
    svn_auth_provider_object_t *prompt_provider = (svn_auth_provider_object_t *)apr_pcalloc (pool, sizeof(*prompt_provider));
	svn_auth_provider_t *vtable = (svn_auth_provider_t *)apr_pcalloc (pool, sizeof(svn_auth_provider_t));
	vtable->cred_kind = SVN_AUTH_CRED_CLIENT_SSL;
	vtable->first_credentials = firstCreds_client_ssl;
	vtable->next_credentials = NULL;
	vtable->save_credentials = NULL;
	prompt_provider->vtable = vtable;
	prompt_provider->provider_baton = that;
	return prompt_provider;
}
svn_auth_provider_object_t *Prompter::getProviderClientSSLPass(Prompter *that)
{
	apr_pool_t *pool = JNIUtil::getRequestPool()->pool();
    svn_auth_provider_object_t *prompt_provider = (svn_auth_provider_object_t *)apr_pcalloc (pool, sizeof(*prompt_provider));
	svn_auth_provider_t *vtable = (svn_auth_provider_t *)apr_pcalloc (pool, sizeof(svn_auth_provider_t));
	vtable->cred_kind = SVN_AUTH_CRED_CLIENT_PASS_SSL;
	vtable->first_credentials = firstCreds_client_ssl_pass;
	vtable->next_credentials = NULL;
	vtable->save_credentials = NULL;
	prompt_provider->vtable = vtable;
	prompt_provider->provider_baton = that;
	return prompt_provider;
}
svn_error_t *Prompter::firstCreds (void **credentials, void **iter_baton,
							void *provider_baton, apr_hash_t *parameters, const char *realmstring, apr_pool_t *pool)
{
	Prompter *that = (Prompter*)provider_baton;

	svn_auth_cred_simple_t *creds = (svn_auth_cred_simple_t *)apr_pcalloc (pool, sizeof(*creds));
	  /* run-time parameters */
	const char *username
		= (const char *)apr_hash_get (parameters, SVN_AUTH_PARAM_DEFAULT_USERNAME,
					APR_HASH_KEY_STRING);
	const char *password
		= (const char *)apr_hash_get (parameters, SVN_AUTH_PARAM_DEFAULT_PASSWORD,
					APR_HASH_KEY_STRING);
	if(username != NULL)
		creds->username = username;
	else
		creds->username = "";
	if(password != NULL)
		creds->password = password;
	else
		creds->password = "";
    *credentials = creds;

	*iter_baton = that;

	if(that != NULL)
	{
		that->m_retry = 0;
		if(realmstring != NULL)
			that->m_realm = realmstring;
		else
			that->m_realm = "";
	}
	return SVN_NO_ERROR;
}
svn_error_t *Prompter::nextCreds (void **credentials, void *iter_baton,
                          apr_hash_t *parameters, apr_pool_t *pool)
{
	Prompter *that = (Prompter*)iter_baton;
	if(that->m_retry >= 2 || that == NULL)
	{
		credentials = NULL;
		return SVN_NO_ERROR;
	}

	const char *username
		= (const char *)apr_hash_get (parameters, SVN_AUTH_PARAM_DEFAULT_USERNAME,
						APR_HASH_KEY_STRING);

	if(!that->prompt(that->m_realm.c_str(), username))
	{
		credentials = NULL;
		return SVN_NO_ERROR;
	}

	jstring juser = that->username();
	jstring jpass = that->password();
	JNIStringHolder user(juser);
	JNIStringHolder pass(jpass);
	if(user != NULL)
		that->m_userName = user;
	else
		that->m_userName = "";
	if(pass != NULL)
		that->m_passWord = pass;
	else
		that->m_passWord = "";
	svn_auth_cred_simple_t *creds = (svn_auth_cred_simple_t *)apr_pcalloc (pool, sizeof(*creds));
    creds->username = that->m_userName.c_str();
    creds->password = that->m_passWord.c_str();
    *credentials = creds;
	return SVN_NO_ERROR;
}
svn_error_t *Prompter::firstCreds_server_ssl (void **credentials, void **iter_baton, 
							void *provider_baton, apr_hash_t *parameters, const char *realmstring, apr_pool_t *pool)
{
	Prompter *that = (Prompter*)provider_baton;
    svn_boolean_t previous_output = FALSE;
    svn_auth_cred_server_ssl_t *cred;
    int failure;
    int failures_in =
        (int) apr_hash_get (parameters,
                        SVN_AUTH_PARAM_SSL_SERVER_FAILURES_IN,
                        APR_HASH_KEY_STRING);

    svn_stringbuf_t *buf = svn_stringbuf_create
        ("Error validating server certificate: ", pool);

    failure = failures_in & SVN_AUTH_SSL_UNKNOWNCA;
    if (failure)
	{
        svn_stringbuf_appendcstr (buf, "Unknown certificate issuer");
        previous_output = TRUE;
    }

    failure = failures_in & SVN_AUTH_SSL_CNMISMATCH;
    if (failure)
    {
        if (previous_output)
        {
            svn_stringbuf_appendcstr (buf, ", ");
        }
        svn_stringbuf_appendcstr (buf, "Hostname mismatch");
        previous_output = TRUE;
    } 
    failure = failures_in & (SVN_AUTH_SSL_EXPIRED | SVN_AUTH_SSL_NOTYETVALID);
    if (failure)
	{
        if (previous_output)
        {
            svn_stringbuf_appendcstr (buf, ", ");
        }
        svn_stringbuf_appendcstr (buf, "Certificate expired or not yet valid");
        previous_output = TRUE;
    }

    svn_stringbuf_appendcstr (buf, ". Accept? (y/N): ");
	if(that->askYesNo(realmstring, buf->data, false))
    {
        cred = (svn_auth_cred_server_ssl_t*)apr_palloc (pool, sizeof(*cred));
        cred->failures_allow = failures_in;
        *credentials = cred;
    }
    else
    {
        *credentials = NULL;
    }
    *iter_baton = NULL;
    return SVN_NO_ERROR;
}
svn_error_t *Prompter::firstCreds_client_ssl (void **credentials, void **iter_baton, 
							void *provider_baton, apr_hash_t *parameters, const char *realmstring, apr_pool_t *pool)
{
	Prompter *that = (Prompter*)provider_baton;
  const char *cert_file = NULL, *key_file = NULL;
  size_t cert_file_len;
  const char *extension;
  svn_auth_cred_client_ssl_t *cred;

  svn_auth_ssl_cert_type_t cert_type;
  cert_file = that->askQuestion(realmstring, "client certificate filename: ", true);
  
  if ((cert_file == NULL) || (cert_file[0] == 0))
    {
      return NULL;
    }

  cert_file_len = strlen(cert_file);
  extension = cert_file + cert_file_len - 4;
  if ((strcmp (extension, ".p12") == 0) || 
      (strcmp (extension, ".P12") == 0))
    {
      cert_type = svn_auth_ssl_pkcs12_cert_type;
    }
  else if ((strcmp (extension, ".pem") == 0) || 
           (strcmp (extension, ".PEM") == 0))
    {
      cert_type = svn_auth_ssl_pem_cert_type;
    }
  else
    {
      const char *type = NULL;
	  type = that->askQuestion(realmstring, "cert type ('pem' or 'pkcs12'): ", true);
      if (type != NULL && (strcmp(type, "pkcs12") == 0) ||
          (strcmp(type, "PKCS12") == 0))
        {
          cert_type = svn_auth_ssl_pkcs12_cert_type;
        }
      else if (type != NULL && (strcmp (type, "pem") == 0) || 
               (strcmp (type, "PEM") == 0))
        {
          cert_type = svn_auth_ssl_pem_cert_type;
        }
      else
        {
          return svn_error_createf (SVN_ERR_INCORRECT_PARAMS, NULL,
                                    "unknown ssl certificate type '%s'", type);
        }
    }
  
  if (cert_type == svn_auth_ssl_pem_cert_type)
    {
	  key_file = that->askQuestion(realmstring, "optional key file: ", true);
    }
  if (key_file && key_file[0] == 0)
    {
      key_file = 0;
    }
  cred = (svn_auth_cred_client_ssl_t*)apr_palloc (pool, sizeof(*cred));
  cred->cert_file = cert_file;
  cred->key_file = key_file;
  cred->cert_type = cert_type;
  *credentials = cred;
  *iter_baton = NULL;
  return SVN_NO_ERROR;
}
svn_error_t *Prompter::firstCreds_client_ssl_pass (void **credentials, void **iter_baton, 
							void *provider_baton, apr_hash_t *parameters, const char *realmstring, apr_pool_t *pool)
{
	Prompter *that = (Prompter*)provider_baton;
  const char *info = NULL;
  info = that->askQuestion(realmstring, "client certificate passphrase: ", false);
  if (info && info[0])
    {
      svn_auth_cred_client_ssl_pass_t *cred = (svn_auth_cred_client_ssl_pass_t*)apr_palloc (pool, sizeof(*cred));
      cred->password = info;
      *credentials = cred;
    }
  else
    {
      *credentials = NULL;
    }
  *iter_baton = NULL;
	return SVN_NO_ERROR;
}
