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


/** Read configuration information from all available sources and merge it 
 * into one @c svn_config_t object.
 *
 * Merge configuration information from all available sources and
 * store it in @a *cfgp, which is allocated in @a pool.  That is, first 
 * read any system-wide configurations (from a file or from the
 * registry), then merge in personal configurations (again from
 * file or registry).
 *
 * ###
 * ### Currently only reads from ~/.subversion/config.
 * ### See http://subversion.tigris.org/issues/show_bug.cgi?id=579.  
 * ###
 *
 * If no config information is available, return an empty @a *cfgp.
 */
svn_error_t *svn_config_read_config (svn_config_t **cfgp, apr_pool_t *pool);


/** Read server config information from all sources and merge it into one
 * @c svn_config_t object.
 *
 * Merge server configuration information from all available sources
 * and store it in @a *cfgp, which is allocated in @a pool.  That is, 
 * first read any system-wide server configurations (from a file or from 
 * the registry), then merge in personal server configurations (again 
 * from file or registry).
 *
 * Under Unix, or a Unix emulator such as Cygwin, personal config is
 * always located in .subversion/proxies in the user's home
 * directory.  Under Windows it may be there, or in the registry; if
 * both are present, the registry is read first and then the file info
 * is merged in.  System config information under Windows is found
 * only in the registry.
 *
 * If no server config information is available, return an empty @a *cfgp.  
 *
 * ### Notes: This function, and future ones like it, rather obviates
 * the need for @c svn_config_read and @c svn_config_merge as public
 * interfaces.  However, I'm leaving them public for now, until it's
 * clear they can be de-exported.  Hmm, funny how in this context, the
 * opposite of "exported" is not "imported", eh?
 */
svn_error_t *svn_config_read_servers (svn_config_t **cfgp, apr_pool_t *pool);


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
