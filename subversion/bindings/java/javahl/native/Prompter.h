/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2004 CollabNet.  All rights reserved.
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
 *
 * @file Prompter.h
 * @brief Interface of the class Prompter
 */

#if !defined(AFX_PROMPTER_H__6833BB77_DDCC_4BF8_A995_5A5CBC48DF4C__INCLUDED_)
#define AFX_PROMPTER_H__6833BB77_DDCC_4BF8_A995_5A5CBC48DF4C__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <jni.h>
#include <svn_auth.h>
#include <string>
class Prompter  
{
private:
	bool m_version2;
	bool m_version3;
	jobject m_prompter;
	Prompter(jobject jprompter, bool v2, bool v3);
	bool prompt(const char *realm, const char *username, bool maySave);
	bool askYesNo(const char *realm, const char *question, bool yesIsDefault);
	const char *askQuestion(const char *realm, const char *question, bool showAnswer, bool maySave);
	int askTrust(const char *question, bool maySave);
	jstring password();
	jstring username();
	static svn_error_t *simple_prompt(svn_auth_cred_simple_t **cred_p, void *baton, 
										const char *realm, const char *username, 
										svn_boolean_t may_save, apr_pool_t *pool);
	static svn_error_t *username_prompt(svn_auth_cred_username_t **cred_p, void *baton,
										const char *realm, svn_boolean_t may_save, 
										apr_pool_t *pool);
	static svn_error_t *ssl_server_trust_prompt(svn_auth_cred_ssl_server_trust_t **cred_p,
										void *baton,const char *realm, apr_uint32_t failures, 
										const svn_auth_ssl_server_cert_info_t *cert_info,
										svn_boolean_t may_save,apr_pool_t *pool);
	static svn_error_t *ssl_client_cert_prompt(svn_auth_cred_ssl_client_cert_t **cred_p,
										void *baton, const char *realm, svn_boolean_t may_save,
										apr_pool_t *pool);
	static svn_error_t *ssl_client_cert_pw_prompt(svn_auth_cred_ssl_client_cert_pw_t **cred_p,
										void *baton, const char *realm, svn_boolean_t may_save,
										apr_pool_t *pool);
	std::string m_answer;
	bool m_maySave;
public:
	static Prompter *makeCPrompter(jobject jpromper);
	~Prompter();
	svn_auth_provider_object_t *getProviderUsername();
	svn_auth_provider_object_t *getProviderSimple();
	svn_auth_provider_object_t *getProviderServerSSLTrust();
	svn_auth_provider_object_t *getProviderClientSSL();
	svn_auth_provider_object_t *getProviderClientSSLPassword();
};

#endif // !defined(AFX_PROMPTER_H__6833BB77_DDCC_4BF8_A995_5A5CBC48DF4C__INCLUDED_)
