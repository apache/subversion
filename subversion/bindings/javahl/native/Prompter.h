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

#ifndef PROMPTER_H
#define PROMPTER_H

#include <jni.h>
#include "svn_auth.h"
#include <string>

/**
 * This class requests username/password and informations about
 * ssl-certificates from the user. There are 3 Java interfaces for that.
 * PromptUserPassword, PromptUserPassword2 and PromptUserPassword3
 * each following interface extends the previous interface.
 */
class Prompter
{
 private:
  /**
   * The Java callback object.
   */
  jobject m_prompter;

  /**
   * The callback objects implements PromptUserPassword2.
   */
  bool m_version2;

  /**
   * The callback objects implements PromptUserPassword3.
   */
  bool m_version3;

  /**
   * Tntermediate storage for an answer.
   */
  std::string m_answer;

  /**
   * Flag is the user allowed, that the last answer is stored in the
   * configuration.
   */
  bool m_maySave;

  Prompter(jobject jprompter, bool v2, bool v3);
  bool prompt(const char *realm, const char *pi_username, bool maySave);
  bool askYesNo(const char *realm, const char *question, bool yesIsDefault);
  const char *askQuestion(const char *realm, const char *question,
                          bool showAnswer, bool maySave);
  int askTrust(const char *question, bool maySave);
  jstring password();
  jstring username();
  static svn_error_t *simple_prompt(svn_auth_cred_simple_t **cred_p,
                                    void *baton, const char *realm,
                                    const char *username,
                                    svn_boolean_t may_save,
                                    apr_pool_t *pool);
  static svn_error_t *username_prompt
    (svn_auth_cred_username_t **cred_p,
     void *baton,
     const char *realm,
     svn_boolean_t may_save,
     apr_pool_t *pool);
  static svn_error_t *ssl_server_trust_prompt
    (svn_auth_cred_ssl_server_trust_t **cred_p,
     void *baton,
     const char *realm,
     apr_uint32_t failures,
     const svn_auth_ssl_server_cert_info_t *cert_info,
     svn_boolean_t may_save,
     apr_pool_t *pool);
  static svn_error_t *ssl_client_cert_prompt
    (svn_auth_cred_ssl_client_cert_t **cred_p,
     void *baton,
     const char *realm,
     svn_boolean_t may_save,
     apr_pool_t *pool);
  static svn_error_t *ssl_client_cert_pw_prompt
    (svn_auth_cred_ssl_client_cert_pw_t **cred_p,
     void *baton,
     const char *realm,
     svn_boolean_t may_save,
     apr_pool_t *pool);
 public:
  static Prompter *makeCPrompter(jobject jprompter);
  ~Prompter();
  svn_auth_provider_object_t *getProviderUsername();
  svn_auth_provider_object_t *getProviderSimple();
  svn_auth_provider_object_t *getProviderServerSSLTrust();
  svn_auth_provider_object_t *getProviderClientSSL();
  svn_auth_provider_object_t *getProviderClientSSLPassword();
};

#endif // PROMPTER_H
