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
#include "org_tigris_subversion_javahl_PromptUserPassword2.h"
#include <svn_client.h>
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

Prompter::Prompter(jobject jprompter, bool v2, bool v3)
{
	m_prompter = jprompter;
	m_version2 = v2;
	m_version3 = v3;
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
	jclass clazz2 = env->FindClass(JAVA_PACKAGE"/PromptUserPassword2");
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	bool v2 = env->IsInstanceOf(jpromper, clazz2) ? true: false;
	env->DeleteLocalRef(clazz2);
	bool v3 = false;
	if(v2)
	{
		jclass clazz3 = env->FindClass(JAVA_PACKAGE"/PromptUserPassword3");
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		v3 = env->IsInstanceOf(jpromper, clazz3) ? true: false;
		env->DeleteLocalRef(clazz3);
	}
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	jobject myPrompt = env->NewGlobalRef(jpromper);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return NULL;
	}
	return new Prompter(myPrompt, v2, v3);
}

jstring Prompter::username()
{
	JNIEnv *env = JNIUtil::getEnv();
	static jmethodID mid = 0;
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
	JNIEnv *env = JNIUtil::getEnv();
	static jmethodID mid = 0;
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
const char *Prompter::askQuestion(const char *realm, const char *question, bool showAnswer, bool maySave)
{
	JNIEnv *env = JNIUtil::getEnv();
	if(m_version3)
	{
		static jmethodID mid = 0;
		static jmethodID mid2 = 0;
		if(mid == 0)
		{
			jclass clazz = env->FindClass(JAVA_PACKAGE"/PromptUserPassword3");
			if(JNIUtil::isJavaExceptionThrown())
			{
				return NULL;
			}
			mid = env->GetMethodID(clazz, "askQuestion", "(Ljava/lang/String;Ljava/lang/String;ZZ)Ljava/lang/String;");
			if(JNIUtil::isJavaExceptionThrown() || mid == 0)
			{
				return NULL;
			}
			mid2 = env->GetMethodID(clazz, "userAllowedSave", "()Z");
			if(JNIUtil::isJavaExceptionThrown() || mid == 0)
			{
				return NULL;
			}
			env->DeleteLocalRef(clazz);
			if(JNIUtil::isJavaExceptionThrown())
			{
				return NULL;
			}
		}

		jstring jrealm = JNIUtil::makeJString(realm);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		jstring jquestion = JNIUtil::makeJString(question);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		jstring janswer = static_cast<jstring>(env->CallObjectMethod(m_prompter, mid, jrealm, jquestion, showAnswer ? JNI_TRUE : JNI_FALSE, 
																	 maySave ? JNI_TRUE : JNI_FALSE));
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		env->DeleteLocalRef(jquestion);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		env->DeleteLocalRef(jrealm);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		JNIStringHolder answer(janswer);
		if(answer != NULL)
		{
			m_answer = answer;
			m_maySave = env->CallBooleanMethod(m_prompter, mid2) ? true: false;
			if(JNIUtil::isJavaExceptionThrown())
			{
				return NULL;
			}
		}
		else
		{
			m_answer = "";
			m_maySave = false;
		}
		return m_answer.c_str();
	}
	else
	{
		static jmethodID mid = 0;
		if(mid == 0)
		{
			jclass clazz = env->FindClass(JAVA_PACKAGE"/PromptUserPassword");
			if(JNIUtil::isJavaExceptionThrown())
			{
				return NULL;
			}
			mid = env->GetMethodID(clazz, "askQuestion", "(Ljava/lang/String;Ljava/lang/String;Z)Ljava/lang/String;");
			if(JNIUtil::isJavaExceptionThrown() || mid == 0)
			{
				return NULL;
			}
			env->DeleteLocalRef(clazz);
			if(JNIUtil::isJavaExceptionThrown())
			{
				return NULL;
			}
		}

		jstring jrealm = JNIUtil::makeJString(realm);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		jstring jquestion = JNIUtil::makeJString(question);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		jstring janswer = static_cast<jstring>(env->CallObjectMethod(m_prompter, mid, jrealm, jquestion, showAnswer ? JNI_TRUE : JNI_FALSE));
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		env->DeleteLocalRef(jquestion);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		env->DeleteLocalRef(jrealm);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return NULL;
		}
		JNIStringHolder answer(janswer);
		if(answer != NULL)
		{
			m_answer = answer;
			if(maySave)
				m_maySave = askYesNo(realm, "May save the answer ?", true);
			else
				m_maySave = false;
		}
		else
		{
			m_answer = "";
			m_maySave = false;
		}
		return m_answer.c_str();
	}
}
int Prompter::askTrust(const char *question, bool maySave)
{
	if(m_version2)
	{
		static jmethodID mid = 0;
		JNIEnv *env = JNIUtil::getEnv();
		if(mid == 0)
		{
			jclass clazz = env->FindClass(JAVA_PACKAGE"/PromptUserPassword2");
			if(JNIUtil::isJavaExceptionThrown())
			{
				return -1;
			}
			mid = env->GetMethodID(clazz, "askTrustSSLServer", "(Ljava/lang/String;Z)I");
			if(JNIUtil::isJavaExceptionThrown() || mid == 0)
			{
				return -1;
			}
			env->DeleteLocalRef(clazz);
			if(JNIUtil::isJavaExceptionThrown())
			{
				return -1;
			}
			jstring jquestion = JNIUtil::makeJString(question);
			if(JNIUtil::isJavaExceptionThrown())
			{
				return -1;
			}
			jint ret = env->CallIntMethod(m_prompter, mid, jquestion, maySave ? JNI_TRUE : JNI_FALSE);
			if(JNIUtil::isJavaExceptionThrown())
			{
				return -1;
			}
			env->DeleteLocalRef(jquestion);
			if(JNIUtil::isJavaExceptionThrown())
			{
				return -1;
			}
			return ret;
		}

	}
	else
	{
		std::string q = question;
		if(maySave)
		{
			q += "(R)eject, accept (t)emporarily or accept (p)ermanently?";
		}
		else
		{
			q += "(R)eject or accept (t)emporarily?";
		}
		const char *answer = askQuestion(NULL, q.c_str(), true, false); 
		if(*answer == 't' || *answer == 'T')
		{
			return org_tigris_subversion_javahl_PromptUserPassword2_AccecptTemporary;
		}
		else if(maySave && (*answer == 'p' || *answer == 'P'))
		{
			return org_tigris_subversion_javahl_PromptUserPassword2_AcceptPermanently;
		}
		else
			return org_tigris_subversion_javahl_PromptUserPassword2_Reject;
	}
	return -1;
}

bool Prompter::prompt(const char *realm, const char *username, bool maySave)
{
	JNIEnv *env = JNIUtil::getEnv();
	if(m_version3)
	{
		static jmethodID mid = 0;
		static jmethodID mid2 = 0;
		if(mid == 0)
		{
			jclass clazz = env->FindClass(JAVA_PACKAGE"/PromptUserPassword3");
			if(JNIUtil::isJavaExceptionThrown())
			{
				return false;
			}
			mid = env->GetMethodID(clazz, "prompt", "(Ljava/lang/String;Ljava/lang/String;Z)Z");
			if(JNIUtil::isJavaExceptionThrown() || mid == 0)
			{
				return false;
			}
			mid2 = env->GetMethodID(clazz, "userAllowedSave", "()Z");
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
        jboolean ret = env->CallBooleanMethod(m_prompter, mid, jrealm, 
                                    jusername, maySave ? JNI_TRUE: JNI_FALSE);
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
		m_maySave = env->CallBooleanMethod(m_prompter, mid2) ? true : false;
		if(JNIUtil::isJavaExceptionThrown())
		{
			return false;
		}
		return ret ? true:false;
	}
	else
	{
		static jmethodID mid = 0;
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
		if(maySave)
			m_maySave = askYesNo(realm, "May save the answer ?", true);
		else
			m_maySave = false;
		return ret ? true:false;
	}
}
svn_auth_provider_object_t *Prompter::getProviderSimple()
{
	apr_pool_t *pool = JNIUtil::getRequestPool()->pool();
	svn_auth_provider_object_t *provider;
	svn_client_get_simple_prompt_provider (&provider,
                                           simple_prompt,
                                           this,
                                           2, /* retry limit */
                                           pool);

	return provider;
}
svn_auth_provider_object_t *Prompter::getProviderUsername()
{
	apr_pool_t *pool = JNIUtil::getRequestPool()->pool();
	svn_auth_provider_object_t *provider;
    svn_client_get_username_prompt_provider (&provider,
                                             username_prompt,
                                             this, 
                                             2, /* retry limit */
                                             pool);

	return provider;
}
svn_auth_provider_object_t *Prompter::getProviderServerSSLTrust()
{
	apr_pool_t *pool = JNIUtil::getRequestPool()->pool();
	svn_auth_provider_object_t *provider;
    svn_client_get_ssl_server_trust_prompt_provider
          (&provider, ssl_server_trust_prompt, this, pool);

	return provider;
}
svn_auth_provider_object_t *Prompter::getProviderClientSSL()
{
	apr_pool_t *pool = JNIUtil::getRequestPool()->pool();
	svn_auth_provider_object_t *provider;
    svn_client_get_ssl_client_cert_prompt_provider
          (&provider, ssl_client_cert_prompt, this, 2, /* retry limit */pool);

	return provider;
}
svn_auth_provider_object_t *Prompter::getProviderClientSSLPassword()
{
	apr_pool_t *pool = JNIUtil::getRequestPool()->pool();
	svn_auth_provider_object_t *provider;
    svn_client_get_ssl_client_cert_pw_prompt_provider
          (&provider, ssl_client_cert_pw_prompt, this, 2, /* retry limit */pool);

	return provider;
}
svn_error_t *Prompter::simple_prompt(svn_auth_cred_simple_t **cred_p, void *baton, 
										const char *realm, const char *username, svn_boolean_t may_save,
										apr_pool_t *pool)
{
	Prompter *that = (Prompter*)baton;
	svn_auth_cred_simple_t *ret = (svn_auth_cred_simple_t*)apr_pcalloc(pool, sizeof(*ret));
	if(!that->prompt(realm, username, may_save ? true : false))
		return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                        "User canceled dialog");
	jstring juser = that->username();
	JNIStringHolder user(juser);
	if(user == NULL)
		return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                        "User canceled dialog");
	ret->username = apr_pstrdup(pool,user);
	jstring jpass = that->password();
	JNIStringHolder pass(jpass);
	if(pass == NULL)
		return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                            "User canceled dialog");
	else
	{
		ret->password  = apr_pstrdup(pool, pass);
		ret->may_save = that->m_maySave;
	}
    *cred_p = ret;
	return SVN_NO_ERROR;
}
svn_error_t *Prompter::username_prompt(svn_auth_cred_username_t **cred_p, void *baton,
										const char *realm, svn_boolean_t may_save, apr_pool_t *pool)
{
	Prompter *that = (Prompter*)baton;
	svn_auth_cred_username_t *ret = (svn_auth_cred_username_t*)apr_pcalloc(pool, sizeof(*ret));
	const char *user = that->askQuestion(realm, "Username: ", true, may_save ? true : false);
	if(user == NULL)
		return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                        "User canceled dialog");
	ret->username = apr_pstrdup(pool,user);
	ret->may_save = that->m_maySave;
    *cred_p = ret;
	return SVN_NO_ERROR;
}
svn_error_t *Prompter::ssl_server_trust_prompt(svn_auth_cred_ssl_server_trust_t **cred_p,
										void *baton,
										const char *realm,
										apr_uint32_t failures, 
										const svn_auth_ssl_server_cert_info_t *cert_info,
										svn_boolean_t may_save,
										apr_pool_t *pool)
{
	Prompter *that = (Prompter*)baton;
	svn_auth_cred_ssl_server_trust_t *ret = (svn_auth_cred_ssl_server_trust_t*)apr_pcalloc(pool, sizeof(*ret));
	
	std::string question = "Error validating server certificate for";
	question += realm;
	question += ":\n";
	
	if(failures & SVN_AUTH_SSL_UNKNOWNCA)
	{
		question += " - Unknown certificate issuer\n";
		question += "   Fingerprint: ";
		question += cert_info->fingerprint;
		question += "\n";
		question += "   Distinguished name: ";
		question += cert_info->issuer_dname;
		question += "\n";
	}

	if(failures & SVN_AUTH_SSL_CNMISMATCH)
	{
		question += " - Hostname mismatch (";
		question += cert_info->hostname;
		question += ")\n";
	}

	if(failures & SVN_AUTH_SSL_NOTYETVALID)
	{
		question += " - Certificate is not yet valid\n";
		question += "   Valid from ";
		question += cert_info->valid_from;
		question += "\n";
	}

	if(failures & SVN_AUTH_SSL_EXPIRED)
	{
		question += " - Certificate is expired\n";
		question += "   Valid until ";
		question += cert_info->valid_until;
		question += "\n";
	}

	switch(that->askTrust(question.c_str(), may_save ? true : false))
	{
	case org_tigris_subversion_javahl_PromptUserPassword2_AccecptTemporary:
	    *cred_p = ret;
		ret->may_save = FALSE;
		break;
	case org_tigris_subversion_javahl_PromptUserPassword2_AcceptPermanently:
	    *cred_p = ret;
		ret->may_save = TRUE;
		ret->accepted_failures = failures;
		break;
	default:
		*cred_p = NULL;
	}
	return SVN_NO_ERROR;
}
svn_error_t *Prompter::ssl_client_cert_prompt(svn_auth_cred_ssl_client_cert_t **cred_p,
										void *baton, const char *realm, svn_boolean_t may_save, 
										apr_pool_t *pool)
{
	Prompter *that = (Prompter*)baton;
	svn_auth_cred_ssl_client_cert_t *ret = (svn_auth_cred_ssl_client_cert_t*)apr_pcalloc(pool, sizeof(*ret));
	const char *cert_file = that->askQuestion(realm, "client certificate filename: ", true, may_save ? true : false);
	if(cert_file == NULL)
		return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                        "User canceled dialog");
	ret->cert_file = apr_pstrdup(pool, cert_file);
	ret->may_save = that->m_maySave;
    *cred_p = ret;
	return SVN_NO_ERROR;
}
svn_error_t *Prompter::ssl_client_cert_pw_prompt(svn_auth_cred_ssl_client_cert_pw_t **cred_p,
										void *baton, const char *realm, svn_boolean_t may_save,
										apr_pool_t *pool)
{
	Prompter *that = (Prompter*)baton;
	svn_auth_cred_ssl_client_cert_pw_t *ret = (svn_auth_cred_ssl_client_cert_pw_t*)apr_pcalloc(pool, sizeof(*ret));
    const char *info = that->askQuestion(realm, "client certificate passphrase: ", false, may_save ? true : false);
	if(info == NULL)
		return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                        "User canceled dialog");
	ret->password = apr_pstrdup(pool, info);
	ret->may_save = that->m_maySave;
    *cred_p = ret;
	return SVN_NO_ERROR;
}

