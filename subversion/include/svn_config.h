/* svn_config.h:  Functions for accessing SVN configuration files.
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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



#ifndef SVN_CONFIG_H
#define SVN_CONFIG_H

#include <apr_pools.h>

#include <svn_types.h>
#include <svn_string.h>
#include <svn_error.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*
   Subversion configuration files
   ==============================

   The syntax of Subversion's configuration files is the same as
   that recognised by Python's ConfigParser module:

      - Empty lines, and lines starting with '#', are ignored.
        The first significant line in a file must be a section header.

      - A section starts with a section header, which must start in
        the first column:

          [section-name]

      - An option, which must always appear within a section, is a pair
        (name, value).  There are two valid forms for defining an
        option, both of which must start in the first column:

          name: value
          name = value

        Whitespace around the separator (:, =) is optional.

      - Section and option names are case-insensitive, but case is
        preserved.

      - An option's value may be broken into several lines.  The value
        continuation lines must start with at least one whitespace.
        Trailing whitespace in the previous line, the newline character
        and the leading whitespace in the continuation line is compressed
        into a single space character.

      - All leading and trailing whitespace around a value is trimmed,
        but the whitespace within a value is preserved, with the
        exception of whitespace around line continuations, as
        described above.

      - Option values may be expanded within a value by enclosing the
        option name in parentheses, preceded by a percent sign:

          %(name)

        The expansion is performed recursively and on demand, during
        svn_option_get.  The name is first searched for in the same
        section, then in the special [DEFAULTS] section. If the name
        is not found, the whole %(name) placeholder is left
        unchanged.

        Any modifications to the configuration data invalidate all
        previously expanded values, so that the next svn_option_get
        will take the modifications into account.


   Configuration data in the Windows registry
   ==========================================

   On Windows, configuration data may be stored in the registry. The
   functions svn_config_read and svn_config_merge will read from the
   registry when passed file names of the form:

      REGISTRY:<hive>/path/to/config-key

   The REGISTRY: prefix must be in upper case. The <hive> part must be
   one of:

      HKLM for HKEY_LOCAL_MACHINE
      HKCU for HKEY_CURRENT_USER

   The values in config-key represent the options in the [DEFAULTS] section.
   The keys below config-key represent other sections, and their values
   represent the options. Only values of type REG_SZ will be used; other
   values, as well as the keys' default values, will be ignored.


   File locations
   ==============

   Typically, Subversion will use two config directories, one for
   site-wide configuration,

     /etc/subversion/proxies
     /etc/subversion/config
     /etc/subversion/hairstyles
        -- or --
     REGISTRY:HKLM\Software\Tigris.org\Subversion\Proxies
     REGISTRY:HKLM\Software\Tigris.org\Subversion\Config
     REGISTRY:HKLM\Software\Tigris.org\Subversion\Hairstyles

   and one for per-user configuration:

     ~/.subversion/proxies
     ~/.subversion/config
     ~/.subversion/hairstyles
        -- or --
     REGISTRY:HKCU\Software\Tigris.org\Subversion\Proxies
     REGISTRY:HKCU\Software\Tigris.org\Subversion\Config
     REGISTRY:HKCU\Software\Tigris.org\Subversion\Hairstyles

*/


/* Opaque structure describing a set of configuration options. */
typedef struct svn_config_t svn_config_t;


/* Merge proxy configuration information from all available sources
   and store it in *CFGP, which is allocated in POOL.  That is, first
   read any system-wide proxy configurations (from a file or from the
   registry), then merge in personal proxy configurations (again from
   file or registry).

   Under Unix, or a Unix emulator such as Cygwin, personal config is
   always located in .subversion/proxies in the user's home
   directory.  Under Windows it may be there, or in the registry; if
   both are present, the registry is read first and then the file info
   is merged in.  System config information under Windows is found
   only in the registry.

   If no proxy config information is available, return an empty *CFGP.  

   ### Notes: This function, and future ones like it, rather obviates
   the need for svn_config_read() and svn_config_merge() as public
   interfaces.  However, I'm leaving them public for now, until it's
   clear they can be de-exported.  Hmm, funny how in this context, the
   opposite of "exported" is not "imported", eh?
*/
svn_error_t *svn_config_read_proxies (svn_config_t **cfgp, apr_pool_t *pool);


/* Read configuration data from FILE (a file or registry path) into
   *CFGP, allocated in POOL.

   If FILE does not exist, then if MUST_EXIST, return an error,
   otherwise return an empty svn_config_t. */
svn_error_t *svn_config_read (svn_config_t **cfgp,
                              const char *file,
                              svn_boolean_t must_exist,
                              apr_pool_t *pool);

/* Like svn_config_read, but merge the configuration data from FILE (a
   file or registry path) into *CFG, which was previously returned
   from svn_config_read.  This function invalidates all value
   expansions in CFG, so that the next svn_option_get() takes the
   modifications into account. */
svn_error_t *svn_config_merge (svn_config_t *cfg,
                               const char *file,
                               svn_boolean_t must_exist);


/* Find the value of a (SECTION, OPTION) pair in CFG, and set
   VALUEP->data to the value and VALUEP->len to the value's length.

   If the value does not exist, return DEFAULT_VALUE.  Otherwise, the
   value returned in VALUEP remains valid at least until the next
   operation that invalidates variable expansions.

   This function may change CFG by expanding option values. */
void svn_config_get (svn_config_t *cfg, svn_string_t *valuep,
                     const char *section, const char *option,
                     const char *default_value);

/* Add or replace the value of a (SECTION, OPTION) pair in CFG with VALUE.
   This function invalidates all value expansions in CFG. */
void svn_config_set (svn_config_t *cfg,
                     const char *section, const char *option,
                     const char *value);


/* Enumerate the options in SECTION, passing BATON and the current
   option's name and value to CALLBACK.  Continue the enumeration if
   CALLBACK returns TRUE.  Return the number of times CALLBACK was
   called.

   ### kff asks: A more usual interface is to continue enumerating
       while CALLBACK does not return error, and if CALLBACK does
       return error, to return the same error (or a wrapping of it)
       from svn_config_enumerate().  What's the use case for
       svn_config_enumerate()?  Is it more likely to need to break out
       of an enumeration early, with no error, than an invocation of
       CALLBACK is likely to need to return an error? ###

   CALLBACK's NAME and VALUE parameters are only valid for the
   duration of the call. */

typedef svn_boolean_t (*svn_config_enumerator_t)
       (const char *name, const svn_string_t *value, void *baton);

int svn_config_enumerate (svn_config_t *cfg, const char *section,
                          svn_config_enumerator_t callback, void *baton);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CONFIG_H */


/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
