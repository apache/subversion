/** 
 * @copyright
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
 * @brief Functions for accessing SVN configuration files.
 */



#ifndef SVN_CONFIG_H
#define SVN_CONFIG_H

#include <apr_pools.h>

#include <svn_types.h>
#include <svn_error.h>


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

#define SVN_CONFIG_CATEGORY_SERVERS        "servers"
#define SVN_CONFIG_SECTION_GROUPS               "groups"
#define SVN_CONFIG_SECTION_GLOBAL               "global"
#define SVN_CONFIG_OPTION_HTTP_PROXY_HOST           "http-proxy-host"
#define SVN_CONFIG_OPTION_HTTP_PROXY_PORT           "http-proxy-port"
#define SVN_CONFIG_OPTION_HTTP_PROXY_USERNAME       "http-proxy-username"
#define SVN_CONFIG_OPTION_HTTP_PROXY_PASSWORD       "http-proxy-password"
#define SVN_CONFIG_OPTION_HTTP_PROXY_EXCEPTIONS     "http-proxy-exceptions"
#define SVN_CONFIG_OPTION_HTTP_TIMEOUT              "http-timeout"
#define SVN_CONFIG_OPTION_HTTP_COMPRESSION          "http-compression"
#define SVN_CONFIG_OPTION_NEON_DEBUG_MASK           "neon-debug-mask"
#define SVN_CONFIG_OPTION_SVN_TUNNEL_AGENT          "svn-tunnel-agent"

#define SVN_CONFIG_CATEGORY_CONFIG          "config"
#define SVN_CONFIG_SECTION_AUTH                 "auth"
#define SVN_CONFIG_OPTION_STORE_PASSWORD            "store-password"
#define SVN_CONFIG_SECTION_HELPERS              "helpers"
#define SVN_CONFIG_OPTION_EDITOR_CMD                "editor-cmd"
#define SVN_CONFIG_OPTION_DIFF_CMD                  "diff-cmd"
#define SVN_CONFIG_OPTION_DIFF3_CMD                 "diff3-cmd"
#define SVN_CONFIG_OPTION_DIFF3_HAS_PROGRAM_ARG     "diff3-has-program-arg"
#define SVN_CONFIG_SECTION_MISCELLANY           "miscellany"
#define SVN_CONFIG_OPTION_GLOBAL_IGNORES            "global-ignores"
#define SVN_CONFIG_OPTION_TEMPLATE_ROOT             "template-root"


/** Read configuration information from the standard sources and
 * return it in the hash @a *cfg_hash.
 *
 * Read configuration information from the standard sources and merge
 * it into the hash @a *cfg_hash.  That is, first read any system-wide
 * configurations (from a file or from the registry), then merge in
 * personal configurations (again from file or registry).  The hash
 * and all its data are allocated in @a pool.
 *
 * @a *cfg_hash is a hash whose keys are @c const char * configuration
 * categories (SVN_CONFIG_CATEGORY_SERVERS,
 * SVN_CONFIG_CATEGORY_CONFIG, etc.) and whose values are the @c
 * svn_config_t * items representing the configuration values for that
 * category.  
 */
svn_error_t *svn_config_get_config (apr_hash_t **cfg_hash, 
                                    apr_pool_t *pool);


/** Read configuration data from a @a file.
 *
 * Read configuration data from @a file (a file or registry path) into
 * @a *cfgp, allocated in @a pool.
 *
 * If @a file does not exist, then if @a must_exist, return an error,
 * otherwise return an empty @c svn_config_t.
 */
svn_error_t *svn_config_read (svn_config_t **cfgp,
                              const char *file,
                              svn_boolean_t must_exist,
                              apr_pool_t *pool);

/** Merge config data from a @a file into an @c svn_config_t.
 *
 * Like @c svn_config_read, but merge the configuration data from @a file 
 * (a file or registry path) into @a *cfg, which was previously returned
 * from @c svn_config_read.  This function invalidates all value
 * expansions in @a cfg, so that the next @c svn_option_get takes the
 * modifications into account.
 */
svn_error_t *svn_config_merge (svn_config_t *cfg,
                               const char *file,
                               svn_boolean_t must_exist);


/** Find a config option's setting.
 *
 * Find the value of a (@a section, @a option) pair in @a cfg, set @a 
 * *valuep to the value.
 *
 * If @a cfg is @c NULL, just sets @a *valuep to @a default_value.
 *
 * If the value does not exist, return @a default_value.  Otherwise, the
 * value returned in @a valuep remains valid at least until the next
 * operation that invalidates variable expansions.  @a default_value may
 * be the same as @a *valuep.
 *
 * This function may change @a cfg by expanding option values.
 */
void svn_config_get (svn_config_t *cfg, const char **valuep,
                     const char *section, const char *option,
                     const char *default_value);

/** Set a config option.
 *
 * Add or replace the value of a (@a section, @a option) pair in @a cfg with 
 * @a value.
 *
 * This function invalidates all value expansions in @a cfg.
 */
void svn_config_set (svn_config_t *cfg,
                     const char *section, const char *option,
                     const char *value);


/** A callback function used in enumerating config options.
 *
 * See @c svn_config_enumerate for the details of this type.
 */
typedef svn_boolean_t (*svn_config_enumerator_t)
       (const char *name, const char *value, void *baton);

/** Enumerate the options in @a section by calling @a callback and passing 
 * it @a baton for each of them.
 *
 * Enumerate the options in @a section, passing @a baton and the current
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
 *     CALLBACK is likely to need to return an error? ###
 *
 * @a callback's @a name and @a name parameters are only valid for the
 * duration of the call.
 */
int svn_config_enumerate (svn_config_t *cfg, const char *section,
                          svn_config_enumerator_t callback, void *baton);


/** Enumerate the group @a master_Section in @a cfg.  Each variable
 * value is interpreted as a list of glob patterns (separated by comma
 * and optional whitespace).  Return the name of the first variable
 * whose value matches @a key, or @c NULL if no variable matches.
 */
const char *svn_config_find_group (svn_config_t *cfg, const char *key,
                                   const char *master_section,
                                   apr_pool_t *pool);


/** Ensure that the user's ~/.subversion/ area exists, and create no-op 
 * template files for any absent config files.
 *
 * Try to ensure that the user's ~/.subversion/ area exists, and create no-op
 * template files for any absent config files.  Use @a pool for any
 * temporary allocation.  
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
svn_error_t *svn_config_ensure (apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CONFIG_H */
