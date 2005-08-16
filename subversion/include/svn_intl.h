/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2005 CollabNet.  All rights reserved.
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
 * @file svn_intl.h
 * @brief Internationalization and localization for Subversion.
 * @since New in 1.3.
 */


#ifndef SVN_INTL_H
#define SVN_INTL_H

#include <apr_errno.h>

#include "svn_error.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Initialize the library, using @a parent_pool to acquire a sub-pool
   for storage of localization bundle data.  A @c NULL @a parent_pool
   indicates that the global pool should be used.  The lifetime of the
   resources used by this module can be managed via @a parent_pool. */
svn_error_t *
svn_intl_initialize (apr_pool_t *parent_pool);

/* Gets the locale preferences for the current context, falling back
   to the system locale for the current process if no user preferences
   have been set.  Returns a list of locales ordered by preference.
   Never returns @c NULL, but may return an empty array
   (e.g. consisting only of a single @c NULL value). */
const char **
svn_intl_get_locale_prefs (apr_pool_t *pool);

/* Sets the locale preferences for the current user.  @a locale_prefs
   are inspected in order for a matching resource bundle.  Not
   invoking this API, or invoking it with a @c NULL locale, or finding
   no match against the preferences will result in the global locale
   being used instead. */
void
svn_intl_set_locale_prefs (char **locale_prefs, apr_pool_t *pool);

/* Retrieve the text identified by @a msgid for the text bundle
   corresponding to @a domain and @a locale. */
const char *
svn_intl_dlgettext (const char *domain, const char *locale, const char *msgid);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_INTL_H */
