/*
 * propset-cmd.c -- Set property values on files/dirs
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

/* ==================================================================== */



/*** Includes. ***/

#include "svn_cmdline.h"
#include "svn_pools.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_utf.h"
#include "svn_subst.h"
#include "svn_path.h"
#include "svn_props.h"
#include "svn_ebcdic.h"
#include "cl.h"

#include "svn_private_config.h"


/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__propset (apr_getopt_t *os,
                 void *baton,
                 apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = ((svn_cl__cmd_baton_t *) baton)->opt_state;
  svn_client_ctx_t *ctx = ((svn_cl__cmd_baton_t *) baton)->ctx;
  const char *pname, *pname_utf8;
  svn_string_t *propval = NULL;
  svn_boolean_t propval_came_from_cmdline;
  apr_array_header_t *args, *targets;
  int i;

  /* PNAME and PROPVAL expected as first 2 arguments if filedata was
     NULL, else PNAME alone will precede the targets.  Get a UTF-8
     version of the name, too. */
  SVN_ERR (svn_opt_parse_num_args (&args, os,
                                   opt_state->filedata ? 1 : 2, pool));
  pname = ((const char **) (args->elts))[0];
  SVN_ERR (svn_utf_cstring_to_utf8 (&pname_utf8, pname, pool));

  /* Get the PROPVAL from either an external file, or from the command
     line. */
  if (opt_state->filedata)
    {
      propval = svn_string_create_from_buf (opt_state->filedata, pool);
      propval_came_from_cmdline = FALSE;
    }
  else
    {
      propval = svn_string_create (((const char **) (args->elts))[1], pool);
      propval_came_from_cmdline = TRUE;
    }
  
  /* We only want special Subversion property values to be in UTF-8
     and LF line endings.  All other propvals are taken literally. */
#if !APR_CHARSET_EBCDIC
  if (svn_prop_needs_translation (pname_utf8))
#else
  /* On ebcdic platforms a file used to set the value of a property
   * may be encoded in ebcdic.  This presents a host of problems re how
   * to detect this case.  Obtaining the file's CCSID is not easy, and
   * even if it were it may not always be accurate (e.g. a CCSID 37 file
   * copied via a mapped drive in Windows Explorer has a 1252 CCSID).
   * To avoid problems and keep things relatively simple, the ebcdic port
   * currently requires that file data used to set svn:* property values be
   * encoded in utf-8 only.
   * 
   * With -F args restricted to utf-8 there's nothing to translate re
   * encoding, but line endings may be inconsistent so translation is still
   * needed.  The problem is if opt_state->encoding is passed,
   * svn_subst_translate_string will attempt to convert propval from a
   * native string to utf-8, corrupting it on the iSeries where
   * native == ebcdic != subset of utf-8.  So "1208" is passed causing no
   * encoding conversion, but producing uniform LF line endings.
   * 
   * See svn_utf_cstring_to_utf8_ex for why a string representation of a
   * CCSID is used rather than "UTF-8". */
  if (opt_state->filedata && svn_prop_needs_translation (pname_utf8))
    SVN_ERR (svn_subst_translate_string (&propval, propval, "1208", pool));
  else if (svn_prop_needs_translation (pname_utf8))
#endif
    SVN_ERR (svn_subst_translate_string (&propval, propval,
                                         opt_state->encoding, pool));
#if APR_CHARSET_EBCDIC
  else if(!opt_state->filedata)
    /* On ebcdic platforms all other propvals are *not* taken
     * literally; these too are converted to utf-8. */
    SVN_ERR (svn_utf_string_to_utf8 (&propval, propval, pool));
#endif
  else 
    if (opt_state->encoding)
      return svn_error_create 
        (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
         _("Bad encoding option: prop value not stored as UTF8"));
  
  /* Suck up all the remaining arguments into a targets array */
  SVN_ERR (svn_opt_args_to_target_array2 (&targets, os, 
                                          opt_state->targets, pool));

  if (opt_state->revprop)  /* operate on a revprop */
    {
      svn_revnum_t rev;
      const char *URL;

      /* Implicit "." is okay for revision properties; it just helps
         us find the right repository. */
      svn_opt_push_implicit_dot_target (targets, pool);

      SVN_ERR (svn_cl__revprop_prepare (&opt_state->start_revision, targets,
                                        &URL, pool));

      /* Let libsvn_client do the real work. */
      SVN_ERR (svn_client_revprop_set (pname_utf8, propval,
                                       URL, &(opt_state->start_revision),
                                       &rev, opt_state->force, ctx, pool));
      if (! opt_state->quiet) 
        {
          SVN_ERR
            (SVN_CMDLINE_PRINTF2
             (pool, _("property '%s' set on repository revision %ld\n"),
              pname_utf8, rev));
        }      
    }
  else if (opt_state->start_revision.kind != svn_opt_revision_unspecified)
    {
      return svn_error_createf
        (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
         _("Cannot specify revision for setting versioned property '%s'"),
#if !APR_CHARSET_EBCDIC
         pname);
#else
         /* ebcdic port assumes var string args to svn_error_creatf are utf-8 */
         pname_utf8);
#endif
    }
  else  /* operate on a normal, versioned property (not a revprop) */
    {
      apr_pool_t *subpool = svn_pool_create (pool);

      /* The customary implicit dot rule has been prone to user error
       * here.  People would do intuitive things like
       * 
       *    $ svn propset svn:executable script
       *
       * and then be surprised to get an error like:
       *
       *    svn: Illegal target for the requested operation
       *    svn: Cannot set svn:executable on a directory ()
       *
       * So we don't do the implicit dot thing anymore.  A * target
       * must always be explicitly provided when setting a versioned
       * property.  See 
       *
       *    http://subversion.tigris.org/issues/show_bug.cgi?id=924
       *
       * for more details.
       */

      if (targets->nelts == 0)
        {
          if (propval_came_from_cmdline)
            {
              return svn_error_createf
                (SVN_ERR_CL_INSUFFICIENT_ARGS, NULL,
                 _("Explicit target required ('%s' interpreted as prop value)"),
                 propval->data);
            }
          else
            {
              return svn_error_create
                (SVN_ERR_CL_INSUFFICIENT_ARGS, NULL,
                 _("Explicit target argument required"));
            }
        }

      for (i = 0; i < targets->nelts; i++)
        {
          const char *target = ((const char **) (targets->elts))[i];
          svn_boolean_t success;

          svn_pool_clear (subpool);
          SVN_ERR (svn_cl__check_cancel (ctx->cancel_baton));
          SVN_ERR (svn_cl__try (svn_client_propset2 (pname_utf8,
                                                     propval, target,
                                                     opt_state->recursive,
                                                     opt_state->force,
                                                     ctx, subpool),
                                &success, opt_state->quiet,
                                SVN_ERR_UNVERSIONED_RESOURCE,
                                SVN_ERR_ENTRY_NOT_FOUND,
                                SVN_NO_ERROR));

          if (success && (! opt_state->quiet))
            {
#if !APR_CHARSET_EBCDIC
              SVN_ERR
                (svn_cmdline_printf
                 (pool, opt_state->recursive
                  ? _("property '%s' set (recursively) on '%s'\n")
                  : _("property '%s' set on '%s'\n"),
                  pname, svn_path_local_style (target, pool)));
#else
              SVN_ERR
                (svn_cmdline_printf_ebcdic2
                 (pool, opt_state->recursive
                  ? _("property '%s' set (recursively) on '%s'\n")
                  : _("property '%s' set on '%s'\n"),
                  pname_utf8, svn_path_local_style (target, pool)));
#endif
            }
        }
      svn_pool_destroy (subpool);
    }

  return SVN_NO_ERROR;
}
