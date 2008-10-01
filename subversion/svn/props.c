/*
 * props.c: Utility functions for property handling
 *
 * ====================================================================
 * Copyright (c) 2000-2008 CollabNet.  All rights reserved.
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

/* ==================================================================== */



/*** Includes. ***/

#include <apr_hash.h>
#include "svn_cmdline.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_subst.h"
#include "svn_props.h"
#include "svn_string.h"
#include "svn_opt.h"
#include "svn_xml.h"
#include "svn_base64.h"
#include "cl.h"

#include "private/svn_cmdline_private.h"

#include "svn_private_config.h"



svn_error_t *
svn_cl__revprop_prepare(const svn_opt_revision_t *revision,
                        apr_array_header_t *targets,
                        const char **URL,
                        apr_pool_t *pool)
{
  const char *target;

  if (revision->kind != svn_opt_revision_number
      && revision->kind != svn_opt_revision_date
      && revision->kind != svn_opt_revision_head)
    return svn_error_create
      (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
       _("Must specify the revision as a number, a date or 'HEAD' "
         "when operating on a revision property"));

  /* There must be exactly one target at this point.  If it was optional and
     unspecified by the user, the caller has already added the implicit '.'. */
  if (targets->nelts != 1)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                            _("Wrong number of targets specified"));

  /* (The docs say the target must be either a URL or implicit '.', but
     explicit WC targets are also accepted.) */
  target = APR_ARRAY_IDX(targets, 0, const char *);
  SVN_ERR(svn_client_url_from_path(URL, target, pool));
  if (*URL == NULL)
    return svn_error_create
      (SVN_ERR_UNVERSIONED_RESOURCE, NULL,
       _("Either a URL or versioned item is required"));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_cl__print_prop_hash(apr_hash_t *prop_hash,
                        svn_boolean_t names_only,
                        apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, prop_hash); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      const char *pname;
      svn_string_t *propval;
      const char *pname_stdout;

      apr_hash_this(hi, &key, NULL, &val);
      pname = key;
      propval = val;

      if (svn_prop_needs_translation(pname))
        SVN_ERR(svn_subst_detranslate_string(&propval, propval,
                                             TRUE, pool));

      SVN_ERR(svn_cmdline_cstring_from_utf8(&pname_stdout, pname, pool));

      /* ### We leave these printfs for now, since if propval wasn't translated
       * above, we don't know anything about its encoding.  In fact, it
       * might be binary data... */
      printf("  %s\n", pname_stdout);
      if (!names_only)
        {
          /* Add an extra newline to the value before indenting, so that
           * every line of output has the indentation whether the value
           * already ended in a newline or not. */
          const char *newval = apr_psprintf(pool, "%s\n", propval->data);

          printf("%s", svn_cl__indent_string(newval, "    ", pool));
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_cl__print_xml_prop_hash(svn_stringbuf_t **outstr,
                            apr_hash_t *prop_hash,
                            svn_boolean_t names_only,
                            apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  if (*outstr == NULL)
    *outstr = svn_stringbuf_create("", pool);

  for (hi = apr_hash_first(pool, prop_hash); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      const char *pname;
      svn_string_t *propval;

      apr_hash_this(hi, &key, NULL, &val);
      pname = key;
      propval = val;

      if (names_only)
        {
          svn_xml_make_open_tag(outstr, pool, svn_xml_self_closing, "property",
                                "name", pname, NULL);
        }
      else
        {
          const char *pname_out;

          if (svn_prop_needs_translation(pname))
            SVN_ERR(svn_subst_detranslate_string(&propval, propval,
                                                 TRUE, pool));

          SVN_ERR(svn_cmdline_cstring_from_utf8(&pname_out, pname, pool));

          svn_cmdline__print_xml_prop(outstr, pname_out, propval, pool);
        }
    }

    return SVN_NO_ERROR;
}


void
svn_cl__check_boolean_prop_val(const char *propname, const char *propval,
                               apr_pool_t *pool)
{
  svn_stringbuf_t *propbuf;

  if (!svn_prop_is_boolean(propname))
    return;

  propbuf = svn_stringbuf_create(propval, pool);
  svn_stringbuf_strip_whitespace(propbuf);

  if (propbuf->data[0] == '\0'
      || strcmp(propbuf->data, "no") == 0
      || strcmp(propbuf->data, "off") == 0
      || strcmp(propbuf->data, "false") == 0)
    {
      svn_error_t *err = svn_error_createf
        (SVN_ERR_BAD_PROPERTY_VALUE, NULL,
         _("To turn off the %s property, use 'svn propdel';\n"
           "setting the property to '%s' will not turn it off."),
           propname, propval);
      svn_handle_warning2(stderr, err, "svn: ");
      svn_error_clear(err);
    }
}

