/*
 * translate.h :  eol and keyword translation
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


#include <apr_pools.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"



/* Newline and keyword translation properties */

/* Valid states for 'svn:eol-style' property.  
   Property nonexistence is equivalent to 'none'. */
enum svn_wc__eol_style
{
  svn_wc__eol_style_unknown, /* An unrecognized style */
  svn_wc__eol_style_none,    /* EOL translation is "off" or ignored value */
  svn_wc__eol_style_native,  /* Translation is set to client's native style */
  svn_wc__eol_style_fixed    /* Translation is set to one of LF, CR, CRLF */
};

/* The text-base eol style for files using svn_wc__eol_style_native
   style.  */
#define SVN_WC__DEFAULT_EOL_MARKER "\n"


/* Query the SVN_PROP_EOL_STYLE property on a file at PATH, and set
   *STYLE to PATH's eol style (one of the three values: none, native,
   or fixed), and set *EOL to

      - NULL if *STYLE is svn_wc__eol_style_none, or

      - a null-terminated C string containing the native eol marker
        for this platform, if *STYLE is svn_wc__eol_style_native, or

      - a null-terminated C string containing the eol marker indicated
        by the property value, if *STYLE is svn_wc__eol_style_fixed.

   If non-null, *EOL is a static string, not allocated in POOL.

   Use POOL for temporary allocation.
*/
svn_error_t *svn_wc__get_eol_style (enum svn_wc__eol_style *style,
                                    const char **eol,
                                    const char *path,
                                    apr_pool_t *pool);


/* Variant of svn_wc__get_eol_style, but without the path argument.
   It assumes that you already have the property VALUE.  This is for
   more "abstract" callers that just want to know how values map to
   EOL styles. */
void svn_wc__eol_style_from_value (enum svn_wc__eol_style *style,
                                   const char **eol,
                                   const char *value);

/* Reverse parser.  Given a real EOL string ("\n", "\r", or "\r\n"),
   return an encoded *VALUE ("LF", "CR", "CRLF") that one might see in
   the property value. */
void svn_wc__eol_value_from_string (const char **value,
                                    const char *eol);

/* Expand keywords for the file at PATH, by parsing a
   whitespace-delimited list of keywords.  If any keywords are found
   in the list, allocate *KEYWORDS from POOL, and then populate its
   entries with the related keyword values (also allocated in POOL).
   If no keywords are found in the list, or if there is no list, set
   *KEYWORDS to NULL.

   If FORCE_LIST is non-null, use it as the list; else use the
   SVN_PROP_KEYWORDS property for PATH.  In either case, use PATH to
   expand keyword values.  If a keyword is in the list, but no
   corresponding value is available, set that element of *KEYWORDS to
   the empty string ("").
*/
svn_error_t *svn_wc__get_keywords (svn_wc_keywords_t **keywords,
                                   const char *path,
                                   const char *force_list,
                                   apr_pool_t *pool);



/* Return a new string, allocated in POOL, containing just the
 * human-friendly portion of DATE.  Subversion date strings typically
 * contain more information than humans want, for example
 *
 *   "Mon 28 Jan 2002 16:17:09.777994 (day 028, dst 0, gmt_off -21600)"
 *   
 * would be converted to
 *
 *   "Mon 28 Jan 2002 16:17:09"
 */
svn_string_t *svn_wc__friendly_date (const char *date, apr_pool_t *pool);



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
