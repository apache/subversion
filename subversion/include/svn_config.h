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
        (name, value). There are two valid forms for defining an option,
        both of which must start in the first column:

          name: value
          name = value

        Whitespace around the separator (:, =) is optional.

      - Section and option names are case-insensitive, but case is
        preserved.

      - An option's value may be broken into several lines. The value
        continuation lines must start with at least one whitespace.
        trailing whitespace in the previous line, the newline character
        and the leading whitespace in the continuation line is compressed
        into a single space character.

      - All leading and trailing whitespace in a value is trimmed, but
        the whitespace within a value is preserved, with the exception
        of whitespace around line continuations, as described above.

      - Option values may be expanded within a value by enclosing the
        option name in parentheses, preceded by a percent sign:

          %(name)

        The expansion is performed recursively and on demand, during
        svn_option_get. The name is first searched for in the same section,
        then in the special [DEFAULTS] section. If the name is not found,
        the whole %(name) placeholder is left unchanged.

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

   Typically, Subversion will use two config files: One for site-wide
   configuration,

     /etc/svn.conf    or
     REGISTRY:HKLM\Software\Tigris.org\Subversion\Config

   and one for per-user configuration:

     ~/.svnrc         or
     REGISTRY:HKCU\Software\Tigris.org\Subversion\Config */


/* Opaque structure describing a set of configuration options. */
typedef struct svn_config_t svn_config_t;



/* Read configuration data from FILE into CFGP. Allocate from a
   sub-pool of POOL.

   If FILE does not exist, then if MUST_EXIST, return an error,
   otherwise return an empty svn_config_t. */
svn_error_t *svn_config_read (svn_config_t **cfgp,
                              const char *file,
                              svn_boolean_t must_exist,
                              apr_pool_t *pool);

/* Like svn_config_read, but merge the configuration data from FILE
   into CFG, which was previously returned from svn_config_read.
   This function invalidates all value expansions in CFG. */
svn_error_t *svn_config_merge (svn_config_t *cfg,
                               const char *file,
                               svn_boolean_t must_exist);


/* Destroy CFG. */
void svn_config_destroy (svn_config_t *cfg);


/* Find the value of a (SECTION, OPTION) pair in CFG, returning a copy in
   VALUEP. If the value does not exist, return DEFAULT_VALUE.  The string
   returned in VALUEP will remain valid at least until the next operation
   that invalidates variable expansions.
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
   CALLBACK returns TRUE. Return the number of times CALLBACK was
   called.

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
