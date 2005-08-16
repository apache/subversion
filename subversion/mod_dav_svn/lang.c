/*
 * lang.c: mod_dav_svn repository provider language negotiation impl.
 *
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
 */



#include <stdlib.h>     /* for qsort */
#include <apr_tables.h>
#include <apr_lib.h>    /* for apr_isspace */
#include <httpd.h>      /* for ap_get_token */

#include "svn_error.h"

/* --------------- Borrowed from httpd's mod_negotiation.c -------------- */

typedef struct accept_rec {
  char *name;                 /* MUST be lowercase */
  float quality;
} accept_rec;

/*
 * Get a single mime type entry --- one media type and parameters;
 * enter the values we recognize into the argument accept_rec
 */

static const char *get_entry(apr_pool_t *p, accept_rec *result,
                             const char *accept_line)
{
    result->quality = 1.0f;

    /*
     * Note that this handles what I gather is the "old format",
     *
     *    Accept: text/html text/plain moo/zot
     *
     * without any compatibility kludges --- if the token after the
     * MIME type begins with a semicolon, we know we're looking at parms,
     * otherwise, we know we aren't.  (So why all the pissing and moaning
     * in the CERN server code?  I must be missing something).
     */

    result->name = ap_get_token(p, &accept_line, 0);
    ap_str_tolower(result->name);     /* You want case insensitive,
                                       * you'll *get* case insensitive.
                                       */

    while (*accept_line == ';')
      {
        /* Parameters ... */

        char *parm;
        char *cp;
        char *end;

        ++accept_line;
        parm = ap_get_token(p, &accept_line, 1);

        /* Look for 'var = value' --- and make sure the var is in lcase. */

        for (cp = parm; (*cp && !apr_isspace(*cp) && *cp != '='); ++cp)
          {
            *cp = apr_tolower(*cp);
          }

        if (!*cp)
          {
            continue;           /* No '='; just ignore it. */
          }

        *cp++ = '\0';           /* Delimit var */
        while (*cp && (apr_isspace(*cp) || *cp == '='))
          {
            ++cp;
          }

        if (*cp == '"')
          {
            ++cp;
            for (end = cp;
                 (*end && *end != '\n' && *end != '\r' && *end != '\"');
                 end++);
          }
        else
          {
            for (end = cp; (*end && !apr_isspace(*end)); end++);
          }
        if (*end)
          {
            *end = '\0';        /* strip ending quote or return */
          }
        ap_str_tolower(cp);

        if (parm[0] == 'q'
            && (parm[1] == '\0' || (parm[1] == 's' && parm[2] == '\0')))
          {
            result->quality = atoq(cp);
          }
      }

    if (*accept_line == ',')
      {
        ++accept_line;
      }

    return accept_line;
}

/* @a accept_line is the Accpet-Language header, which is of the
   format:

     Accept-Language: name; q=N;
*/
static apr_array_header_t *do_header_line(apr_pool_t *p,
                                          const char *accept_line)
{
    apr_array_header_t *accept_recs;

    if (!accept_line)
      return NULL;

    accept_recs = apr_array_make(p, 10, sizeof(accept_rec));

    while (*accept_line)
      {
        accept_rec *lang_prefs = (accept_rec *) apr_array_push(accept_recs);
        accept_line = get_entry(p, lang_prefs, accept_line);
      }

    return accept_recs;
}

/* ---------------------------------------------------------------------- */


/* qsort implementation for the quality field of the accept_rec
   structure */
static int sort_lang_pref(const void *accept_rec1, const void *accept_rec2)
{
  float diff = ((accept_rec *) accept_rec1)->quality -
      ((accept_rec *) accept_rec2)->quality;
  return (diff == 0 ? 0 : (diff > 0 ? 1 : -1));
}

/* It would be nice if this function could be unit-tested.  Paul
   Querna suggested
   http://svn.apache.org/repos/asf/httpd/httpd/trunk/server/request.c's
   make_sub_request(), and noted that httpd manually constructs
   request_rec's in a few spots. */

svn_error_t * svn_dav__negotiate_lang_prefs(request_rec *r)
{
  char **lang_prefs = NULL;
  int i;

  /* It would be nice if mod_negotiation
     <http://httpd.apache.org/docs-2.1/mod/mod_negotiation.html> could
     handle the Accept-Language header parsing for us.  Sadly, it
     looks like its data structures and routines are private (see
     httpd/modules/mappers/mod_negotiation.c).  Thus, we duplicate the
     necessary ones in this file. */

  apr_array_header_t *a =
    do_header_line(r->pool, apr_table_get(r->headers_in, "Accept-Language"));

  if (apr_is_empty_array(a))
    return SVN_NO_ERROR;

  qsort(a->elts, (size_t) a->nelts, sizeof(accept_rec), sort_lang_pref);
  if (a->nelts > 0)
    {
      lang_prefs = apr_pcalloc(r->pool, (a->nelts + 1) * sizeof(*lang_prefs));
      for (i = 0; i < a->nelts; i++)
        {
          lang_prefs[i] = ((accept_rec **) (a->elts))[i]->name;
        }
    }
  svn_intl_set_locale_prefs(lang_prefs, r->pool);

  return SVN_NO_ERROR;
}
