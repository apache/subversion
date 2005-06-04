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

/* gettext msgid used to request the current locale using gettext. */
#define SVN_CLIENT_MESSAGE_LOCALE "Client requests untranslated messages"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Initialize the library, using @a parent_pool to acquire a sub-pool
   for storage of localization bundle data.  A NULL @a parent_pool
   indicates that the global pool should be used.  The lifetime of the
   resources used by this module can be managed via @a parent_pool. */
apr_status_t
svn_intl_initialize (apr_pool_t *parent_pool);

/* Sets the locale preferences for @a context.  @a locale_prefs are
   inspected in order for a matching resource bundle.  Not invoking
   this API, or invoking it with a NULL locale, or finding no match
   against the preferences will result in the global locale being used
   instead. */
void
svn_intl_set_locale_prefs (void *context, char **locale_prefs);

/* Retrieve the text identified by @a msgid for the text bundle
   corresponding to @a domain and @a locale. */
const char *
svn_intl_dlgettext (const char *domain, const char *locale, const char *msgid);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_INTL_H */
