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
	jobject m_prompter;
	Prompter(jobject jprompter);
	bool prompt(const char *realm, const char *username);
	bool askYesNo(const char *realm, const char *question, bool yesIsDefault);
	const char *askQuestion(const char *realm, const char *question, bool showAnswer);
	jstring password();
	jstring username();
	static svn_error_t *firstCreds (void **credentials, void **iter_baton, 
							void *provider_baton, apr_hash_t *parameters, const char *realmstring, apr_pool_t *pool);
	static svn_error_t *nextCreds (void **credentials, void *iter_baton,
                          apr_hash_t *parameters, apr_pool_t *pool);
	static svn_error_t *firstCreds_server_ssl (void **credentials, void **iter_baton, 
							void *provider_baton, apr_hash_t *parameters, const char *realmstring, apr_pool_t *pool);
	static svn_error_t *firstCreds_client_ssl (void **credentials, void **iter_baton, 
							void *provider_baton, apr_hash_t *parameters, const char *realmstring, apr_pool_t *pool);
	static svn_error_t *firstCreds_client_ssl_pass (void **credentials, void **iter_baton, 
							void *provider_baton, apr_hash_t *parameters, const char *realmstring, apr_pool_t *pool);
	int m_retry;
    std::string m_userName;
    std::string m_passWord;
	std::string m_realm;
	std::string m_answer;
public:
	static Prompter *makeCPrompter(jobject jpromper);
	~Prompter();
	static svn_auth_provider_object_t *getProvider(Prompter *that);
	static svn_auth_provider_object_t *getProviderServerSSL(Prompter *that);
	static svn_auth_provider_object_t *getProviderClientSSL(Prompter *that);
	static svn_auth_provider_object_t *getProviderClientSSLPass(Prompter *that);
};

#endif // !defined(AFX_PROMPTER_H__6833BB77_DDCC_4BF8_A995_5A5CBC48DF4C__INCLUDED_)
