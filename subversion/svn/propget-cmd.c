/*
 * propget-cmd.c -- Print properties and values of files/dirs
 *
 * ====================================================================
 * Copyright (c) 2000-2007 CollabNet.  All rights reserved.
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

#include "svn_cmdline.h"
#include "svn_pools.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error_codes.h"
#include "svn_error.h"
#include "svn_utf.h"
#include "svn_subst.h"
#include "svn_path.h"
#include "svn_props.h"
#include "svn_xml.h"
#include "cl.h"

#include "private/svn_cmdline_private.h"

#include "svn_private_config.h"


/*** Code. ***/

static svn_error_t *
stream_write(svn_stream_t *out,
             const char *data,
             apr_size_t len)
{
  apr_size_t write_len = len;

  /* We're gonna bail on an incomplete write here only because we know
     that this stream is really stdout, which should never be blocking
     on us. */
  SVN_ERR(svn_stream_write(out, data, &write_len));
  if (write_len != len)
    return svn_error_create(SVN_ERR_STREAM_UNEXPECTED_EOF, NULL,
                            "Error writing to stream");
  return SVN_NO_ERROR;
}


static svn_error_t *
print_properties_xml(const char *pname,
                     apr_hash_t *props,
                     apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(pool);

  for (hi = apr_hash_first(pool, props); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      const char *filename;
      svn_string_t *propval;
      svn_stringbuf_t *sb = NULL;

      svn_pool_clear(iterpool);
      apr_hash_this(hi, &key, NULL, &val);
      filename = key;
      propval = val;

      svn_xml_make_open_tag(&sb, iterpool, svn_xml_normal, "target",
                        "path", filename, NULL);
      svn_cmdline__print_xml_prop(&sb, pname, propval, iterpool);
      svn_xml_make_close_tag(&sb, iterpool, "target");

      SVN_ERR(svn_cl__error_checked_fputs(sb->data, stdout));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* Print the properties in PROPS to the stream OUT. PROPS is a hash mapping
 * (const char *) path to (svn_string_t) property value.
 * If IS_URL is true, all paths are URLs, else all paths are local paths.
 * PNAME_UTF8 is the property name of all the properties.
 * If PRINT_FILENAMES is true, print the item's path before each property.
 * If OMIT_NEWLINE is true, don't add a newline at the end of each property.
 * If LIKE_PROPLIST is true, print everything in a more verbose format
 * like "svn proplist -v" does.
 * */
static svn_error_t *
print_properties(svn_stream_t *out,
                 svn_boolean_t is_url,
                 const char *pname_utf8,
                 apr_hash_t *props,
                 svn_boolean_t print_filenames,
                 svn_boolean_t omit_newline,
                 svn_boolean_t like_proplist,
                 apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(pool);

  for (hi = apr_hash_first(pool, props); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      const char *filename;
      svn_string_t *propval;

      svn_pool_clear(iterpool);
      apr_hash_this(hi, &key, NULL, &val);
      filename = key;
      propval = val;

      if (print_filenames)
        {
          const char *header;

          /* Print the file name. */

          if (! is_url)
            filename = svn_path_local_style(filename, iterpool);

          /* In verbose mode, print exactly same as "proplist" does;
           * otherwise, print a brief header. */
          header = apr_psprintf(iterpool, like_proplist
                                ? _("Properties on '%s':\n")
                                : "%s - ", filename);
          SVN_ERR(svn_cmdline_cstring_from_utf8(&header, header, iterpool));
          SVN_ERR(stream_write(out, header, strlen(header)));
        }

      if (like_proplist)
        {
          /* Print the property name and value just as "proplist -v" does */
          apr_hash_t *hash = apr_hash_make(iterpool);

          apr_hash_set(hash, pname_utf8, APR_HASH_KEY_STRING, propval);
          svn_cl__print_prop_hash(hash, FALSE, iterpool);
        }
      else
        {
          /* If this is a special Subversion property, it is stored as
             UTF8, so convert to the native format. */
          if (svn_prop_needs_translation(pname_utf8))
            SVN_ERR(svn_subst_detranslate_string(&propval, propval,
                                                 TRUE, iterpool));

          SVN_ERR(stream_write(out, propval->data, propval->len));

          if (! omit_newline)
            SVN_ERR(stream_write(out, APR_EOL_STR,
                                 strlen(APR_EOL_STR)));
        }
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__propget(apr_getopt_t *os,
                void *baton,
                apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  const char *pname, *pname_utf8;
  apr_array_header_t *args, *targets;
  svn_stream_t *out;
  int i;

  if (opt_state->verbose && (opt_state->revprop || opt_state->strict
                             || opt_state->xml))
    return svn_error_create(SVN_ERR_CL_MUTUALLY_EXCLUSIVE_ARGS, NULL,
                            _("--verbose cannot be used with --revprop or "
                              "--strict or --xml"));

  /* PNAME is first argument (and PNAME_UTF8 will be a UTF-8 version
     thereof) */
  SVN_ERR(svn_opt_parse_num_args(&args, os, 1, pool));
  pname = APR_ARRAY_IDX(args, 0, const char *);
  SVN_ERR(svn_utf_cstring_to_utf8(&pname_utf8, pname, pool));
  if (! svn_prop_name_is_valid(pname_utf8))
    return svn_error_createf(SVN_ERR_CLIENT_PROPERTY_NAME, NULL,
                             _("'%s' is not a valid Subversion property name"),
                             pname_utf8);

  SVN_ERR(svn_cl__args_to_target_array_print_reserved(&targets, os,
                                                      opt_state->targets,
                                                      ctx, pool));

  /* Add "." if user passed 0 file arguments */
  svn_opt_push_implicit_dot_target(targets, pool);

  /* Open a stream to stdout. */
  SVN_ERR(svn_stream_for_stdout(&out, pool));

  if (opt_state->revprop)  /* operate on a revprop */
    {
      svn_revnum_t rev;
      const char *URL;
      svn_string_t *propval;

      SVN_ERR(svn_cl__revprop_prepare(&opt_state->start_revision, targets,
                                      &URL, pool));

      /* Let libsvn_client do the real work. */
      SVN_ERR(svn_client_revprop_get(pname_utf8, &propval,
                                     URL, &(opt_state->start_revision),
                                     &rev, ctx, pool));

      if (propval != NULL)
        {
          if (opt_state->xml)
            {
              svn_stringbuf_t *sb = NULL;
              char *revstr = apr_psprintf(pool, "%ld", rev);

              SVN_ERR(svn_cl__xml_print_header("properties", pool));

              svn_xml_make_open_tag(&sb, pool, svn_xml_normal,
                                    "revprops",
                                    "rev", revstr, NULL);

              svn_cmdline__print_xml_prop(&sb, pname_utf8, propval, pool);

              svn_xml_make_close_tag(&sb, pool, "revprops");

              SVN_ERR(svn_cl__error_checked_fputs(sb->data, stdout));
              SVN_ERR(svn_cl__xml_print_footer("properties", pool));
            }
          else
            {
              svn_string_t *printable_val = propval;

              /* If this is a special Subversion property, it is stored as
                 UTF8 and LF, so convert to the native locale and eol-style. */

              if (svn_prop_needs_translation(pname_utf8))
                SVN_ERR(svn_subst_detranslate_string(&printable_val, propval,
                                                     TRUE, pool));

              SVN_ERR(stream_write(out, printable_val->data,
                                   printable_val->len));
              if (! opt_state->strict)
                SVN_ERR(stream_write(out, APR_EOL_STR, strlen(APR_EOL_STR)));
            }
        }
    }
  else  /* operate on a normal, versioned property (not a revprop) */
    {
      apr_pool_t *subpool = svn_pool_create(pool);

      if (opt_state->xml)
        SVN_ERR(svn_cl__xml_print_header("properties", subpool));

      if (opt_state->depth == svn_depth_unknown)
        opt_state->depth = svn_depth_empty;

      /* Strict mode only makes sense for a single target.  So make
         sure we have only a single target, and that we're not being
         asked to recurse on that target. */
      if (opt_state->strict
          && ((targets->nelts > 1) || (opt_state->depth != svn_depth_empty)))
        return svn_error_create
          (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
           _("Strict output of property values only available for single-"
             "target, non-recursive propget operations"));

      for (i = 0; i < targets->nelts; i++)
        {
          const char *target = APR_ARRAY_IDX(targets, i, const char *);
          apr_hash_t *props;
          svn_boolean_t print_filenames;
          svn_boolean_t omit_newline;
          svn_boolean_t like_proplist;
          const char *truepath;
          svn_opt_revision_t peg_revision;

          svn_pool_clear(subpool);
          SVN_ERR(svn_cl__check_cancel(ctx->cancel_baton));

          /* Check for a peg revision. */
          SVN_ERR(svn_opt_parse_path(&peg_revision, &truepath, target,
                                     subpool));

          SVN_ERR(svn_client_propget3(&props, pname_utf8, truepath,
                                      &peg_revision,
                                      &(opt_state->start_revision),
                                      NULL, opt_state->depth,
                                      opt_state->changelists, ctx, subpool));

          /* Any time there is more than one thing to print, or where
             the path associated with a printed thing is not obvious,
             we'll print filenames.  That is, unless we've been told
             not to do so with the --strict option. */
          print_filenames = ((opt_state->depth > svn_depth_empty
                              || targets->nelts > 1
                              || apr_hash_count(props) > 1
                              || opt_state->verbose)
                             && (! opt_state->strict));
          omit_newline = opt_state->strict;
          like_proplist = opt_state->verbose && !opt_state->strict;

          if (opt_state->xml)
            print_properties_xml(pname_utf8, props, subpool);
          else
            print_properties(out, svn_path_is_url(target), pname_utf8, props,
                             print_filenames, omit_newline, like_proplist,
                             subpool);
        }

      if (opt_state->xml)
        SVN_ERR(svn_cl__xml_print_footer("properties", subpool));

      svn_pool_destroy(subpool);
    }

  return SVN_NO_ERROR;
}
