/** 
 * @copyright
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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
 * @file svn_config.h
 * @brief Accessing SVN configuration files.
 */



#ifndef SVN_CONFIG_H
#define SVN_CONFIG_H

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/**************************************************************************
 ***                                                                    ***
 ***  For a description of the SVN configuration file syntax, see       ***
 ***  your ~/.subversion/README, which is written out automatically by  ***
 ***  svn_config_ensure().                                              ***
 ***                                                                    ***
 **************************************************************************/


/** Opaque structure describing a set of configuration options. */
typedef struct svn_config_t svn_config_t;


/*** Configuration Defines ***/

/** @{
 * Strings for the names of files, sections, and options in the
 * client configuration files.
 */
#define SVN_CONFIG_CATEGORY_SERVERS \
        "\x73\x65\x72\x76\x65\x72\x73"  
        /* "servers" */

#define SVN_CONFIG_SECTION_GROUPS \
        "\x67\x72\x6f\x75\x70\x73"      
        /* "groups" */

#define SVN_CONFIG_SECTION_GLOBAL \
        "\x67\x6c\x6f\x62\x61\x6c"      
        /* "global" */

#define SVN_CONFIG_OPTION_HTTP_PROXY_HOST \
        "\x68\x74\x74\x70\x2d\x70\x72\x6f\x78\x79\x2d\x68\x6f\x73\x74"
        /* "http-proxy-host" */

#define SVN_CONFIG_OPTION_HTTP_PROXY_PORT \
        "\x68\x74\x74\x70\x2d\x70\x72\x6f\x78\x79\x2d\x70\x6f\x72\x74"
        /* "http-proxy-port" */

#define SVN_CONFIG_OPTION_HTTP_PROXY_USERNAME \
        "\x68\x74\x74\x70\x2d\x70\x72\x6f\x78\x79\x2d\x75\x73\x65\x72" \
        "\x6e\x61\x6d\x65"  
        /* "http-proxy-username" */

#define SVN_CONFIG_OPTION_HTTP_PROXY_PASSWORD \
        "\x68\x74\x74\x70\x2d\x70\x72\x6f\x78\x79\x2d\x70\x61\x73\x73" \
        "\x77\x6f\x72\x64"
        /* "http-proxy-password" */

#define SVN_CONFIG_OPTION_HTTP_PROXY_EXCEPTIONS \
        "\x68\x74\x74\x70\x2d\x70\x72\x6f\x78\x79\x2d\x65\x78\x63\x65" \
        "\x70\x74\x69\x6f\x6e\x73"
        /* "http-proxy-exceptions" */

#define SVN_CONFIG_OPTION_HTTP_TIMEOUT \
        "\x68\x74\x74\x70\x2d\x74\x69\x6d\x65\x6f\x75\x74"
        /* "http-timeout" */

#define SVN_CONFIG_OPTION_HTTP_COMPRESSION \
        "\x68\x74\x74\x70\x2d\x63\x6f\x6d\x70\x72\x65\x73\x73\x69\x6f\x6e"
        /* "http-compression" */

#define SVN_CONFIG_OPTION_NEON_DEBUG_MASK \
        "\x6e\x65\x6f\x6e\x2d\x64\x65\x62\x75\x67\x2d\x6d\x61\x73\x6b"
        /* "neon-debug-mask" */

#define SVN_CONFIG_OPTION_SSL_AUTHORITY_FILES \
        "\x73\x73\x6c\x2d\x61\x75\x74\x68\x6f\x72\x69\x74\x79\x2d\x66" \
        "\x69\x6c\x65\x73"
        /* "ssl-authority-files" */

#define SVN_CONFIG_OPTION_SSL_TRUST_DEFAULT_CA \
        "\x73\x73\x6c\x2d\x74\x72\x75\x73\x74\x2d\x64\x65\x66\x61\x75" \
        "\x6c\x74\x2d\x63\x61"
        /* "ssl-trust-default-ca" */

#define SVN_CONFIG_OPTION_SSL_CLIENT_CERT_FILE \
        "\x73\x73\x6c\x2d\x63\x6c\x69\x65\x6e\x74\x2d\x63\x65\x72\x74" \
        "\x2d\x66\x69\x6c\x65"
        /* "ssl-client-cert-file" */

#define SVN_CONFIG_OPTION_SSL_CLIENT_CERT_PASSWORD \
        "\x73\x73\x6c\x2d\x63\x6c\x69\x65\x6e\x74\x2d\x63\x65\x72\x74" \
        "\x2d\x70\x61\x73\x73\x77\x6f\x72\x64"
        /* "ssl-client-cert-password" */

#define SVN_CONFIG_CATEGORY_CONFIG \
        "\x63\x6f\x6e\x66\x69\x67"
        /* "config" */

#define SVN_CONFIG_SECTION_AUTH \
        "\x61\x75\x74\x68"
        /* "auth" */

#define SVN_CONFIG_OPTION_STORE_PASSWORDS \
        "\x73\x74\x6f\x72\x65\x2d\x70\x61\x73\x73\x77\x6f\x72\x64\x73"
        /* "store-passwords" */

#define SVN_CONFIG_OPTION_STORE_AUTH_CREDS \
        "\x73\x74\x6f\x72\x65\x2d\x61\x75\x74\x68\x2d\x63\x72\x65\x64\x73"
        /* "store-auth-creds" */

#define SVN_CONFIG_SECTION_HELPERS \
        "\x68\x65\x6c\x70\x65\x72\x73"
        /* "helpers" */

#define SVN_CONFIG_OPTION_EDITOR_CMD \
        "\x65\x64\x69\x74\x6f\x72\x2d\x63\x6d\x64"
        /* "editor-cmd" */

#define SVN_CONFIG_OPTION_DIFF_CMD \
        "\x64\x69\x66\x66\x2d\x63\x6d\x64"
        /* "diff-cmd" */

#define SVN_CONFIG_OPTION_DIFF3_CMD \
        "\x64\x69\x66\x66\x33\x2d\x63\x6d\x64"
        /* "diff3-cmd" */

#define SVN_CONFIG_OPTION_DIFF3_HAS_PROGRAM_ARG \
        "\x64\x69\x66\x66\x33\x2d\x68\x61\x73\x2d\x70\x72\x6f\x67\x72" \
        "\x61\x6d\x2d\x61\x72\x67"
        /* "diff3-has-program-arg" */

#define SVN_CONFIG_SECTION_MISCELLANY \
        "\x6d\x69\x73\x63\x65\x6c\x6c\x61\x6e\x79"
        /* "miscellany" */

#define SVN_CONFIG_OPTION_GLOBAL_IGNORES \
        "\x67\x6c\x6f\x62\x61\x6c\x2d\x69\x67\x6e\x6f\x72\x65\x73"
        /* "global-ignores" */

#define SVN_CONFIG_OPTION_LOG_ENCODING \
        "\x6c\x6f\x67\x2d\x65\x6e\x63\x6f\x64\x69\x6e\x67"
        /* "log-encoding" */

#define SVN_CONFIG_OPTION_USE_COMMIT_TIMES \
        "\x75\x73\x65\x2d\x63\x6f\x6d\x6d\x69\x74\x2d\x74\x69\x6d\x65\x73"
        /* "use-commit-times" */

#define SVN_CONFIG_OPTION_TEMPLATE_ROOT \
        "\x74\x65\x6d\x70\x6c\x61\x74\x65\x2d\x72\x6f\x6f\x74"
        /* "template-root" */

#define SVN_CONFIG_OPTION_ENABLE_AUTO_PROPS \
        "\x65\x6e\x61\x62\x6c\x65\x2d\x61\x75\x74\x6f\x2d\x70\x72\x6f\x70\x73"
        /* "enable-auto-props" */
        
#define SVN_CONFIG_OPTION_NO_UNLOCK \
        "\x6e\x6f\x2d\x75\x6e\x6c\x6f\x63\x6b"
        /* "no-unlock" */        

#define SVN_CONFIG_SECTION_TUNNELS \
        "\x74\x75\x6e\x6e\x65\x6c\x73"
        /* "tunnels" */

#define SVN_CONFIG_SECTION_AUTO_PROPS \
        "\x61\x75\x74\x6f\x2d\x70\x72\x6f\x70\x73"
        /* "auto-props" */
/** @} */

/** @{
 * Strings for the names of sections and options in the
 * repository conf directory configuration files.
 */
/* For repository svnserve.conf files */
#define SVN_CONFIG_SECTION_GENERAL \
        "\x67\x65\x6e\x65\x72\x61\x6c"
        /* "general" */

#define SVN_CONFIG_OPTION_ANON_ACCESS \
        "\x61\x6e\x6f\x6e\x2d\x61\x63\x63\x65\x73\x73"
        /* "anon-access" */

#define SVN_CONFIG_OPTION_AUTH_ACCESS \
        "\x61\x75\x74\x68\x2d\x61\x63\x63\x65\x73\x73"
        /* "auth-access" */

#define SVN_CONFIG_OPTION_PASSWORD_DB \
        "\x70\x61\x73\x73\x77\x6f\x72\x64\x2d\x64\x62"
        /* "password-db" */

#define SVN_CONFIG_OPTION_REALM \
        "\x72\x65\x61\x6c\x6d"
        /* "realm" */

/* For repository password database */
#define SVN_CONFIG_SECTION_USERS \
        "\x75\x73\x65\x72\x73"
        /* "users" */
/** @} */

/*** Configuration Default Values ***/

#define SVN_CONFIG_DEFAULT_GLOBAL_IGNORES \
        "\x2a\x2e\x6f\x20\x2a\x2e\x6c\x6f\x20\x2a\x2e\x6c\x61\x20\x23" \
        "\x2a\x23\x20\x2e\x2a\x2e\x72\x65\x6a\x20\x2a\x2e\x72\x65\x6a" \
        "\x20\x2e\x2a\x7e\x20\x2a\x7e\x20\x2e\x23\x2a\x20\x2e\x44\x53" \
        "\x5f\x53\x74\x6f\x72\x65"  
        /* "*.o *.lo *.la #*# .*.rej *.rej .*~ *~ .#* .DS_Store" */

#define SVN_CONFIG_TRUE \
        "\x74\x72\x75\x65"
        /* "true" */

#define SVN_CONFIG_FALSE \
        "\x66\x61\x6c\x73\x65"
        /* "false" */


/** Read configuration information from the standard sources and merge it
 * into the hash @a *cfg_hash.  If @a config_dir is not NULL it specifies a
 * directory from which to read the configuration files, overriding all
 * other sources.  Otherwise, first read any system-wide configurations
 * (from a file or from the registry), then merge in personal
 * configurations (again from file or registry).  The hash and all its data
 * are allocated in @a pool.
 *
 * @a *cfg_hash is a hash whose keys are @c const char * configuration
 * categories (@c SVN_CONFIG_CATEGORY_SERVERS,
 * @c SVN_CONFIG_CATEGORY_CONFIG, etc.) and whose values are the @c
 * svn_config_t * items representing the configuration values for that
 * category.  
 */
svn_error_t *svn_config_get_config (apr_hash_t **cfg_hash,
                                    const char *config_dir,
                                    apr_pool_t *pool);


/** Read configuration data from @a file (a file or registry path) into
 * @a *cfgp, allocated in @a pool.
 *
 * If @a file does not exist, then if @a must_exist, return an error,
 * otherwise return an empty @c svn_config_t.
 */
svn_error_t *svn_config_read (svn_config_t **cfgp,
                              const char *file,
                              svn_boolean_t must_exist,
                              apr_pool_t *pool);

/** Like @c svn_config_read, but merges the configuration data from @a file 
 * (a file or registry path) into @a *cfg, which was previously returned
 * from @c svn_config_read.  This function invalidates all value
 * expansions in @a cfg, so that the next @c svn_option_get takes the
 * modifications into account.
 */
svn_error_t *svn_config_merge (svn_config_t *cfg,
                               const char *file,
                               svn_boolean_t must_exist);


/** Find the value of a (@a section, @a option) pair in @a cfg, set @a 
 * *valuep to the value.
 *
 * If @a cfg is @c NULL, just sets @a *valuep to @a default_value. If
 * the value does not exist, expand and return @a default_value.
 *
 * The returned value will be valid at least until the next call to
 * @c svn_config_get, or for the lifetime of @a default_value. It is
 * safest to consume the returned value immediately.
 *
 * This function may change @a cfg by expanding option values.
 */
void svn_config_get (svn_config_t *cfg, const char **valuep,
                     const char *section, const char *option,
                     const char *default_value);

/** Add or replace the value of a (@a section, @a option) pair in @a cfg with 
 * @a value.
 *
 * This function invalidates all value expansions in @a cfg.
 */
void svn_config_set (svn_config_t *cfg,
                     const char *section, const char *option,
                     const char *value);

/** Like @c svn_config_get, but for boolean values.
 *
 * Parses the option as a boolean value. The recognized representations
 * are 'true'/'false', 'yes'/'no', 'on'/'off', '1'/'0'; case does not
 * matter. Returns an error if the option doesn't contain a known string.
 */
svn_error_t *svn_config_get_bool (svn_config_t *cfg, svn_boolean_t *valuep,
                                  const char *section, const char *option,
                                  svn_boolean_t default_value);

/** Like @c svn_config_set, but for boolean values.
 *
 * Sets the option to 'true'/'false', depending on @a value.
 */
void svn_config_set_bool (svn_config_t *cfg,
                          const char *section, const char *option,
                          svn_boolean_t value);

/** A callback function used in enumerating config sections.
 *
 * See @c svn_config_enumerate_sections for the details of this type.
 */
typedef svn_boolean_t (*svn_config_section_enumerator_t)
       (const char *name, void *baton);

/** Enumerate the sections, passing @a baton and the current section's name to
 * @a callback.  Continue the enumeration if @a callback returns @c TRUE.
 * Return the number of times @a callback was called.
 *
 * ### See kff's comment to @c svn_config_enumerate.  It applies to this
 * function, too. ###
 *
 * @a callback's @a name and @a name parameters are only valid for the
 * duration of the call.
 */
int svn_config_enumerate_sections (svn_config_t *cfg, 
                                   svn_config_section_enumerator_t callback,
                                   void *baton);

/** A callback function used in enumerating config options.
 *
 * See @c svn_config_enumerate for the details of this type.
 */
typedef svn_boolean_t (*svn_config_enumerator_t)
       (const char *name, const char *value, void *baton);

/** Enumerate the options in @a section, passing @a baton and the current
 * option's name and value to @a callback.  Continue the enumeration if
 * @a callback returns @c TRUE.  Return the number of times @a callback 
 * was called.
 *
 * ### kff asks: A more usual interface is to continue enumerating
 *     while @a callback does not return error, and if @a callback does
 *     return error, to return the same error (or a wrapping of it)
 *     from @c svn_config_enumerate.  What's the use case for
 *     @c svn_config_enumerate?  Is it more likely to need to break out
 *     of an enumeration early, with no error, than an invocation of
 *     @a callback is likely to need to return an error? ###
 *
 * @a callback's @a name and @a value parameters are only valid for the
 * duration of the call.
 */
int svn_config_enumerate (svn_config_t *cfg, const char *section,
                          svn_config_enumerator_t callback, void *baton);


/** Enumerate the group @a master_section in @a cfg.  Each variable
 * value is interpreted as a list of glob patterns (separated by comma
 * and optional whitespace).  Return the name of the first variable
 * whose value matches @a key, or @c NULL if no variable matches.
 */
const char *svn_config_find_group (svn_config_t *cfg, const char *key,
                                   const char *master_section,
                                   apr_pool_t *pool);

/** Retrieve value corresponding to @a option_name for a given
 *  @a server_group in @a cfg , or return @a default_value if none is found.
 *
 *  The config will first be checked for a default, then will be checked for
 *  an override in a server group.
 */
const char *svn_config_get_server_setting(svn_config_t *cfg,
                                          const char* server_group,
                                          const char* option_name,
                                          const char* default_value);

/** Retrieve value into @a result_value corresponding to @a option_name for a
 *  given @a server_group in @a cfg, or return @a default_value if none is
 *  found.
 *
 *  The config will first be checked for a default, then will be checked for
 *  an override in a server group. If the value found is not a valid integer,
 *  a @c svn_error_t* will be returned.
 */
svn_error_t *svn_config_get_server_setting_int(svn_config_t *cfg,
                                               const char *server_group,
                                               const char *option_name,
                                               apr_int64_t default_value,
                                               apr_int64_t *result_value,
                                               apr_pool_t *pool);


/** Try to ensure that the user's ~/.subversion/ area exists, and create
 * no-op template files for any absent config files.  Use @a pool for any
 * temporary allocation.  If @a config_dir is not NULL it specifies a
 * directory from which to read the config overriding all other sources.
 *
 * Don't error if something exists but is the wrong kind (for example,
 * ~/.subversion exists but is a file, or ~/.subversion/servers exists
 * but is a directory).
 *
 * Also don't error if try to create something and fail -- it's okay
 * for the config area or its contents not to be created.  But if
 * succeed in creating a config template file, return error if unable
 * to initialize its contents.
 */
svn_error_t *svn_config_ensure (const char *config_dir, apr_pool_t *pool);




/** Accessing cached authentication data in the user config area.
 *
 * @defgroup cached_authentication_data cached authentication data.
 * @{
 */


/** A hash-key pointing to a realmstring.  Every file containing
 * authentication data should have this key.
 */
#define SVN_CONFIG_REALMSTRING_KEY \
        "\x73\x76\x6e\x3a\x72\x65\x61\x6c\x6d\x73\x74\x72\x69\x6e\x67"  
        /* "svn:realmstring" */

/** Use @a cred_kind and @a realmstring to locate a file within the
 * ~/.subversion/auth/ area.  If the file exists, initialize @a *hash
 * and load the file contents into the hash, using @a pool.  If the
 * file doesn't exist, set @a *hash to NULL.
 *
 * If @a config_dir is not NULL it specifies a directory from which to
 * read the config overriding all other sources.
 *
 * Besides containing the original credential fields, the hash will
 * also contain @c SVN_CONFIG_REALMSTRING_KEY.  The caller can examine
 * this value as a sanity-check that the correct file was loaded.
 *
 * The hashtable will contain <tt>const char *</tt> keys and
 * <tt>svn_string_t *</tt> values.
 */
svn_error_t * svn_config_read_auth_data (apr_hash_t **hash,
                                         const char *cred_kind,
                                         const char *realmstring,
                                         const char *config_dir,
                                         apr_pool_t *pool);

/** Use @a cred_kind and @a realmstring to create or overwrite a file
 * within the ~/.subversion/auth/ area.  Write the contents of @a hash into
 * the file.  If @a config_dir is not NULL it specifies a directory to read
 * the config overriding all other sources.
 *
 * Also, add @a realmstring to the file, with key @c
 * SVN_CONFIG_REALMSTRING_KEY.  This allows programs (or users) to
 * verify exactly which set credentials live within the file.
 *
 * The hashtable must contain <tt>const char *</tt> keys and
 * <tt>svn_string_t *</tt> values.
 */
svn_error_t * svn_config_write_auth_data (apr_hash_t *hash,
                                          const char *cred_kind,
                                          const char *realmstring,
                                          const char *config_dir,
                                          apr_pool_t *pool);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CONFIG_H */
