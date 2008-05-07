/*
 * merge.c:  merging changes into a working file
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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



#include "svn_wc.h"
#include "svn_diff.h"
#include "svn_path.h"

#include "wc.h"
#include "entries.h"
#include "translate.h"
#include "questions.h"
#include "log.h"

#include "svn_private_config.h"


/* Return a pointer to the svn_prop_t structure from PROP_DIFF
   belonging to PROP_NAME, if any.  NULL otherwise.*/
static const svn_prop_t *
get_prop(const apr_array_header_t *prop_diff,
         const char *prop_name)
{
  if (prop_diff)
    {
      int i;
      for (i = 0; i < prop_diff->nelts; i++)
        {
          const svn_prop_t *elt = &APR_ARRAY_IDX(prop_diff, i, svn_prop_t);
          if (strcmp(elt->name,prop_name) == 0)
            return elt;
        }
    }

  return NULL;
}


/* Detranslate a working copy file MERGE_TARGET to achieve the effect of:

   1. Detranslate
   2. Install new props
   3. Retranslate
   4. Detranslate

   in 1 pass to get a file which can be compared with the left and right
   files which were created with the 'new props' above.

   Property changes make this a little complex though. Changes in

   - svn:mime-type
   - svn:eol-style
   - svn:keywords
   - svn:special

   may change the way a file is translated.

   Effect for svn:mime-type:

     The value for svn:mime-type affects the translation wrt keywords
     and eol-style settings.

   I) both old and new mime-types are texty
      -> just do the translation dance (as lined out below)

   II) the old one is texty, the new one is binary
      -> detranslate with the old eol-style and keywords
         (the new re+detranslation is a no-op)

   III) the old one is binary, the new one texty
      -> detranslate with the new eol-style
         (the old detranslation is a no-op)

   IV) the old and new ones are binary
      -> don't detranslate, just make a straight copy


   Effect for svn:eol-style

   I) On add or change use the new value

   II) otherwise: use the old value (absent means 'no translation')


   Effect for svn:keywords

     Always use old settings (re+detranslation are no-op)


   Effect for svn:special

     Always use the old settings (same reasons as for svn:keywords)

*/
static svn_error_t *
detranslate_wc_file(const char **detranslated_file,
                    const char *merge_target,
                    svn_wc_adm_access_t *adm_access,
                    svn_boolean_t force_copy,
                    const apr_array_header_t *prop_diff,
                    apr_pool_t *pool)
{
  svn_boolean_t is_binary;
  const svn_prop_t *prop;
  svn_subst_eol_style_t style;
  const char *eol;
  apr_hash_t *keywords;
  svn_boolean_t special;

  /* Decide if the merge target currently is a text or binary file. */
  SVN_ERR(svn_wc_has_binary_prop(&is_binary,
                                 merge_target, adm_access, pool));


  /* See if we need to do a straight copy:
     - old and new mime-types are binary, or
     - old mime-type is binary and no new mime-type specified */
  if (is_binary
      && (((prop = get_prop(prop_diff, SVN_PROP_MIME_TYPE))
           && prop->value && svn_mime_type_is_binary(prop->value->data))
          || prop == NULL))
    {
      /* this is case IV above */
      keywords = NULL;
      special = FALSE;
      eol = NULL;
      style = svn_subst_eol_style_none;
    }
  else if ((!is_binary)
           && (prop = get_prop(prop_diff, SVN_PROP_MIME_TYPE))
           && prop->value && svn_mime_type_is_binary(prop->value->data))
    {
      /* Old props indicate texty, new props indicate binary:
         detranslate keywords and old eol-style */
      SVN_ERR(svn_wc__get_keywords(&keywords, merge_target,
                                   adm_access, NULL, pool));
      SVN_ERR(svn_wc__get_special(&special, merge_target, adm_access, pool));
    }
  else
    {
      /* New props indicate texty, regardless of old props */

      /* In case the file used to be special, detranslate specially */
      SVN_ERR(svn_wc__get_special(&special, merge_target, adm_access, pool));

      if (special)
        {
          keywords = NULL;
          eol = NULL;
          style = svn_subst_eol_style_none;
        }
      else
        {
          /* In case a new eol style was set, use that for detranslation */
          if ((prop = get_prop(prop_diff, SVN_PROP_EOL_STYLE)) && prop->value)
            {
              /* Value added or changed */
              svn_subst_eol_style_from_value(&style, &eol, prop->value->data);
            }
          else if (!is_binary)
            SVN_ERR(svn_wc__get_eol_style(&style, &eol, merge_target,
                                          adm_access, pool));
          else
            {
              eol = NULL;
              style = svn_subst_eol_style_none;
            }

          /* In case there were keywords, detranslate with keywords
             (iff we were texty) */
          if (!is_binary)
            SVN_ERR(svn_wc__get_keywords(&keywords, merge_target,
                                         adm_access, NULL, pool));
          else
            keywords = NULL;
        }
    }

  /* Now, detranslate with the settings we created above */

  if (force_copy || keywords || eol || special)
    {
      const char *detranslated;
      /* Force a copy into the temporary wc area to avoid having
         temporary files created below to appear in the actual wc. */

      SVN_ERR(svn_wc_create_tmp_file2
              (NULL, &detranslated,
               svn_wc_adm_access_path(adm_access),
               svn_io_file_del_none, pool));

      SVN_ERR(svn_subst_translate_to_normal_form(merge_target,
                                                 detranslated,
                                                 style,
                                                 eol, eol ? FALSE : TRUE,
                                                 keywords,
                                                 special,
                                                 pool));
      *detranslated_file = detranslated;
    }
  else
    *detranslated_file = merge_target;

  return SVN_NO_ERROR;
}

/* Updates (by copying and translating) the eol style in
   OLD_TARGET returning the filename containing the
   correct eol style in NEW_TARGET, if an eol style
   change is contained in PROP_DIFF */
static svn_error_t *
maybe_update_target_eols(const char **new_target,
                         const char *old_target,
                         svn_wc_adm_access_t *adm_access,
                         const apr_array_header_t *prop_diff,
                         apr_pool_t *pool)
{
  const svn_prop_t *prop = get_prop(prop_diff, SVN_PROP_EOL_STYLE);

  if (prop && prop->value)
    {
      const char *eol;
      const char *tmp_new;

      svn_subst_eol_style_from_value(NULL, &eol, prop->value->data);
      SVN_ERR(svn_wc_create_tmp_file2(NULL, &tmp_new,
                                      svn_wc_adm_access_path(adm_access),
                                      svn_io_file_del_none,
                                      pool));
      SVN_ERR(svn_subst_copy_and_translate3(old_target,
                                            tmp_new,
                                            eol, TRUE,
                                            NULL, FALSE,
                                            FALSE, pool));
      *new_target = tmp_new;
    }
  else
    *new_target = old_target;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__merge_internal(svn_stringbuf_t **log_accum,
                       enum svn_wc_merge_outcome_t *merge_outcome,
                       const char *left,
                       const char *right,
                       const char *merge_target,
                       const char *copyfrom_text,
                       svn_wc_adm_access_t *adm_access,
                       const char *left_label,
                       const char *right_label,
                       const char *target_label,
                       svn_boolean_t dry_run,
                       const char *diff3_cmd,
                       const apr_array_header_t *merge_options,
                       const apr_array_header_t *prop_diff,
                       svn_wc_conflict_resolver_func_t conflict_func,
                       void *conflict_baton,
                       apr_pool_t *pool)
{
  const char *tmp_target, *result_target, *working_text;
  const char *adm_path = svn_wc_adm_access_path(adm_access);
  apr_file_t *result_f;
  svn_boolean_t is_binary = FALSE;
  const svn_wc_entry_t *entry;
  svn_boolean_t contains_conflicts;
  const svn_prop_t *mimeprop;
  svn_diff_file_options_t *options;

  /* Sanity check:  the merge target must be under revision control (unless
     this is an add-with-history). */
  SVN_ERR(svn_wc_entry(&entry, merge_target, adm_access, FALSE, pool));
  if (! entry && ! copyfrom_text)
    {
      *merge_outcome = svn_wc_merge_no_merge;
      return SVN_NO_ERROR;
    }

  /* Decide if the merge target is a text or binary file. */
  if ((mimeprop = get_prop(prop_diff, SVN_PROP_MIME_TYPE))
      && mimeprop->value)
    is_binary = svn_mime_type_is_binary(mimeprop->value->data);
  else if (! copyfrom_text)
    SVN_ERR(svn_wc_has_binary_prop(&is_binary, merge_target, adm_access, pool));

  working_text = copyfrom_text ? copyfrom_text : merge_target;
  SVN_ERR(detranslate_wc_file(&tmp_target, working_text, adm_access,
                              (! is_binary) && diff3_cmd != NULL,
                              prop_diff, pool));

  /* We cannot depend on the left file to contain the same eols as the
     right file. If the merge target has mods, this will mark the entire
     file as conflicted, so we need to compensate. */
  SVN_ERR(maybe_update_target_eols(&left, left, adm_access, prop_diff, pool));

  if (! is_binary)              /* this is a text file */
    {
      /* Open a second temporary file for writing; this is where diff3
         will write the merged results. */
      SVN_ERR(svn_wc_create_tmp_file2(&result_f, &result_target,
                                      adm_path, svn_io_file_del_none,
                                      pool));

      options = svn_diff_file_options_create(pool);

      if (merge_options)
        SVN_ERR(svn_diff_file_options_parse(options, merge_options, pool));


      /* Run an external merge if requested. */
      if (diff3_cmd)
        {
          int exit_code;

          SVN_ERR(svn_io_run_diff3_2(&exit_code, ".",
                                     tmp_target, left, right,
                                     target_label, left_label, right_label,
                                     result_f, diff3_cmd,
                                     merge_options, pool));

          contains_conflicts = exit_code == 1;
        }
      else
        {
          svn_diff_t *diff;
          const char *target_marker;
          const char *left_marker;
          const char *right_marker;
          svn_stream_t *ostream;

          ostream = svn_stream_from_aprfile(result_f, pool);

          SVN_ERR(svn_diff_file_diff3_2(&diff,
                                        left, tmp_target, right,
                                        options, pool));

          /* Labels fall back to sensible defaults if not specified. */
          if (target_label)
            target_marker = apr_psprintf(pool, "<<<<<<< %s", target_label);
          else
            target_marker = "<<<<<<< .working";

          if (left_label)
            left_marker = apr_psprintf(pool, "||||||| %s", left_label);
          else
            left_marker = "||||||| .old";

          if (right_label)
            right_marker = apr_psprintf(pool, ">>>>>>> %s", right_label);
          else
            right_marker = ">>>>>>> .new";

          SVN_ERR(svn_diff_file_output_merge(ostream, diff,
                                             left, tmp_target, right,
                                             left_marker,
                                             target_marker,
                                             right_marker,
                                             "=======", /* seperator */
                                             FALSE, /* display original */
                                             FALSE, /* resolve conflicts */
                                             pool));
          SVN_ERR(svn_stream_close(ostream));

          contains_conflicts = svn_diff_contains_conflicts(diff);
        }

      /* Close the output file */
      SVN_ERR(svn_io_file_close(result_f, pool));

      if (contains_conflicts && ! dry_run)  /* got a conflict */
        {
          const char *left_copy, *right_copy, *target_copy;
          const char *tmp_left, *tmp_right, *tmp_target_copy;
          const char *parentt, *target_base;
          svn_wc_adm_access_t *parent_access;
          svn_wc_entry_t tmp_entry;

          /* Give the conflict resolution callback a chance to clean
             up the conflict before we mark the file 'conflicted' */
          if (conflict_func)
            {
              svn_wc_conflict_result_t *result = NULL;
              svn_wc_conflict_description_t cdesc;

              cdesc.path = merge_target;
              cdesc.node_kind = svn_node_file;
              cdesc.kind = svn_wc_conflict_kind_text;
              cdesc.is_binary = FALSE;
              cdesc.mime_type = (mimeprop && mimeprop->value)
                                  ? mimeprop->value->data : NULL;
              cdesc.access = adm_access;
              cdesc.action = svn_wc_conflict_action_edit;
              cdesc.reason = svn_wc_conflict_reason_edited;
              cdesc.base_file = left;
              cdesc.their_file = right;
              cdesc.my_file = tmp_target;
              cdesc.merged_file = result_target;
              cdesc.property_name = NULL;

              SVN_ERR(conflict_func(&result, &cdesc, conflict_baton, pool));
              if (result == NULL)
                return svn_error_create(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE,
                                        NULL, _("Conflict callback violated API:"
                                                " returned no results"));

              if (result->save_merged)
                {
                  const char *edited_copy;
                  /* ### Should use preserved-conflict-file-exts. */
                  SVN_ERR(svn_io_open_unique_file2(NULL,
                                                   &edited_copy,
                                                   merge_target,
                                                   ".edited",
                                                   svn_io_file_del_none,
                                                   pool));
                  SVN_ERR(svn_wc__loggy_copy
                          (log_accum, NULL, adm_access,
                           svn_wc__copy_translate,
                           /* Look for callback's own merged-file first: */
                           result->merged_file
                           ? result->merged_file : result_target,
                           edited_copy,
                           FALSE, pool));
                }

              switch (result->choice)
                {
                  /* If the callback wants to use one of the fulltexts
                     to resolve the conflict, so be it.*/
                  case svn_wc_conflict_choose_base:
                    {
                      SVN_ERR(svn_wc__loggy_copy
                              (log_accum, NULL, adm_access,
                               svn_wc__copy_translate,
                               left, merge_target,
                               FALSE, pool));
                      *merge_outcome = svn_wc_merge_merged;
                      contains_conflicts = FALSE;
                      goto merge_complete;
                    }
                  case svn_wc_conflict_choose_theirs_full:
                    {
                      SVN_ERR(svn_wc__loggy_copy
                              (log_accum, NULL, adm_access,
                               svn_wc__copy_translate,
                               right, merge_target,
                               FALSE, pool));
                      *merge_outcome = svn_wc_merge_merged;
                      contains_conflicts = FALSE;
                      goto merge_complete;
                    }
                  case svn_wc_conflict_choose_mine_full:
                    {
                      /* Do nothing to merge_target, let it live untouched! */
                      *merge_outcome = svn_wc_merge_merged;
                      contains_conflicts = FALSE;
                      goto merge_complete;
                    }
                  case svn_wc_conflict_choose_theirs_conflict:
                  case svn_wc_conflict_choose_mine_conflict:
                    {
                      apr_file_t *chosen_f;
                      const char *chosen_path;
                      svn_stream_t *chosen_stream;
                      svn_diff_t *diff;
                      svn_diff_conflict_display_style_t style =
                        result->choice == svn_wc_conflict_choose_theirs_conflict
                        ? svn_diff_conflict_display_latest
                        : svn_diff_conflict_display_modified;

                      SVN_ERR(svn_wc_create_tmp_file2(&chosen_f,
                                                      &chosen_path,
                                                      adm_path,
                                                      svn_io_file_del_none,
                                                      pool));
                      chosen_stream = svn_stream_from_aprfile2(chosen_f, FALSE,
                                                               pool);
                      SVN_ERR(svn_diff_file_diff3_2(&diff,
                                                    left, tmp_target, right,
                                                    options, pool));
                      SVN_ERR(svn_diff_file_output_merge2(chosen_stream, diff,
                                                          left,
                                                          tmp_target,
                                                          right,
                                                          /* markers ignored */
                                                          NULL, NULL,
                                                          NULL, NULL,
                                                          style,
                                                          pool));
                      SVN_ERR(svn_stream_close(chosen_stream));
                      SVN_ERR(svn_wc__loggy_copy
                              (log_accum, NULL, adm_access,
                               /* ### XXX: translation needed here? */
                               svn_wc__copy_translate,
                               chosen_path, merge_target,
                               FALSE, pool));
                      *merge_outcome = svn_wc_merge_merged;
                      contains_conflicts = FALSE;
                      goto merge_complete;
                    }

                    /* For the case of 3-way file merging, we don't
                       really distinguish between these return values;
                       if the callback claims to have "generally
                       resolved" the situation, we still interpret
                       that as "OK, we'll assume the merged version is
                       good to use". */
                  case svn_wc_conflict_choose_merged:
                    {
                      SVN_ERR(svn_wc__loggy_copy
                              (log_accum, NULL, adm_access,
                               svn_wc__copy_translate,
                               /* Look for callback's own merged-file first: */
                               result->merged_file ?
                                  result->merged_file : result_target,
                               merge_target,
                               FALSE, pool));
                      *merge_outcome = svn_wc_merge_merged;
                      contains_conflicts = FALSE;
                      goto merge_complete;
                    }
                  case svn_wc_conflict_choose_postpone:
                  default:
                    {
                      /* Assume conflict remains, fall through to code below. */
                    }
                }
            }

          /* Preserve the three pre-merge files, and modify the
             entry (mark as conflicted, track the preserved files). */

          /* I miss Lisp. */

          SVN_ERR(svn_io_open_unique_file2(NULL,
                                           &left_copy,
                                           merge_target,
                                           left_label,
                                           svn_io_file_del_none,
                                           pool));

          /* Have I mentioned how much I miss Lisp? */

          SVN_ERR(svn_io_open_unique_file2(NULL,
                                           &right_copy,
                                           merge_target,
                                           right_label,
                                           svn_io_file_del_none,
                                           pool));

          /* Why, how much more pleasant to be forced to unroll my loops.
             If I'd been writing in Lisp, I might have mapped an inline
             lambda form over a list, or something equally disgusting.
             Thank goodness C was here to protect me! */

          SVN_ERR(svn_io_open_unique_file2(NULL,
                                           &target_copy,
                                           merge_target,
                                           target_label,
                                           svn_io_file_del_none,
                                           pool));

          /* We preserve all the files with keywords expanded and line
             endings in local (working) form. */

          svn_path_split(target_copy, &parentt, &target_base, pool);
          SVN_ERR(svn_wc_adm_retrieve(&parent_access, adm_access, parentt,
                                      pool));

          /* Log files require their paths to be in the subtree
             relative to the adm_access path they are executed in.

             Make our LEFT and RIGHT files 'local' if they aren't... */
          if (! svn_path_is_child(adm_path, left, pool))
            {
              SVN_ERR(svn_wc_create_tmp_file2
                      (NULL, &tmp_left,
                       adm_path, svn_io_file_del_none, pool));
              SVN_ERR(svn_io_copy_file(left, tmp_left, TRUE, pool));
            }
          else
            tmp_left = left;

          if (! svn_path_is_child(adm_path, right, pool))
            {
              SVN_ERR(svn_wc_create_tmp_file2
                      (NULL, &tmp_right,
                       adm_path, svn_io_file_del_none, pool));
              SVN_ERR(svn_io_copy_file(right, tmp_right, TRUE, pool));
            }
          else
            tmp_right = right;

          /* NOTE: Callers must ensure that the svn:eol-style and
             svn:keywords property values are correct in the currently
             installed props.  With 'svn merge', it's no big deal.  But
             when 'svn up' calls this routine, it needs to make sure that
             this routine is using the newest property values that may
             have been received *during* the update.  Since this routine
             will be run from within a log-command, merge_file()
             needs to make sure that a previous log-command to 'install
             latest props' has already executed first.  Ben and I just
             checked, and that is indeed the order in which the log items
             are written, so everything should be fine.  Really.  */

          /* Create LEFT and RIGHT backup files, in expanded form.
             We use merge_target's current properties to do the translation. */
          /* Derive the basenames of the 3 backup files. */
          SVN_ERR(svn_wc__loggy_translated_file(log_accum,
                                                adm_access,
                                                left_copy, tmp_left,
                                                merge_target, pool));
          SVN_ERR(svn_wc__loggy_translated_file(log_accum,
                                                adm_access,
                                                right_copy, tmp_right,
                                                merge_target, pool));

          /* Back up MERGE_TARGET through detranslation/retranslation:
             the new translation properties may not match the current ones */
          SVN_ERR(svn_wc_translated_file2(&tmp_target_copy,
                                          merge_target,
                                          merge_target,
                                          adm_access,
                                          SVN_WC_TRANSLATE_TO_NF
                                          | SVN_WC_TRANSLATE_NO_OUTPUT_CLEANUP,
                                          pool));
          SVN_ERR(svn_wc__loggy_translated_file
                  (log_accum, adm_access,
                   target_copy, tmp_target_copy, merge_target, pool));

          tmp_entry.conflict_old
            = svn_path_is_child(adm_path, left_copy, pool);
          tmp_entry.conflict_new
            = svn_path_is_child(adm_path, right_copy, pool);
          tmp_entry.conflict_wrk = target_base;

          /* Mark merge_target's entry as "Conflicted", and start tracking
             the backup files in the entry as well. */
          SVN_ERR(svn_wc__loggy_entry_modify
                  (log_accum, adm_access,
                   merge_target, &tmp_entry,
                   SVN_WC__ENTRY_MODIFY_CONFLICT_OLD
                   | SVN_WC__ENTRY_MODIFY_CONFLICT_NEW
                   | SVN_WC__ENTRY_MODIFY_CONFLICT_WRK,
                   pool));

          *merge_outcome = svn_wc_merge_conflict;
        }
      else if (contains_conflicts && dry_run)
        {
          *merge_outcome = svn_wc_merge_conflict;
        } /* end of conflict handling */
      else if (copyfrom_text)
        {
          *merge_outcome = svn_wc_merge_merged;
        }
      else
        {
          svn_boolean_t same;
          SVN_ERR(svn_io_files_contents_same_p(&same, result_target,
                                               merge_target, pool));

          *merge_outcome = same ? svn_wc_merge_unchanged : svn_wc_merge_merged;
        }

      if (*merge_outcome != svn_wc_merge_unchanged && ! dry_run)
        /* replace MERGE_TARGET with the new merged file, expanding. */
        SVN_ERR(svn_wc__loggy_copy(log_accum, NULL,
                                   adm_access,
                                   svn_wc__copy_translate,
                                   result_target, merge_target,
                                   FALSE, pool));

    } /* end of merging for text files */

  else if (! dry_run) /* merging procedure for binary files */
    {
      /* ### when making the binary-file backups, should we be honoring
         keywords and eol stuff?   */

      const char *left_copy, *right_copy;
      const char *parentt, *left_base, *right_base;
      svn_wc_entry_t tmp_entry;

      /* Give the conflict resolution callback a chance to clean
         up the conflict before we mark the file 'conflicted' */
      if (conflict_func)
        {
          svn_wc_conflict_result_t *result = NULL;
          svn_wc_conflict_description_t cdesc;

          cdesc.path = merge_target;
          cdesc.node_kind = svn_node_file;
          cdesc.kind = svn_wc_conflict_kind_text;
          cdesc.is_binary = TRUE;
          cdesc.mime_type = (mimeprop && mimeprop->value)
                                ? mimeprop->value->data : NULL;
          cdesc.access = adm_access;
          cdesc.action = svn_wc_conflict_action_edit;
          cdesc.reason = svn_wc_conflict_reason_edited;
          cdesc.base_file = left;
          cdesc.their_file = right;
          cdesc.my_file = tmp_target;
          cdesc.merged_file = NULL;     /* notice there is NO merged file! */
          cdesc.property_name = NULL;

          SVN_ERR(conflict_func(&result, &cdesc, conflict_baton, pool));
          if (result == NULL)
            return svn_error_create(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE,
                                    NULL, _("Conflict callback violated API:"
                                            " returned no results"));

          switch (result->choice)
            {
              /* For a binary file, there's no merged file to look at,
                 unless the conflict-callback did the merging itself. */
              case svn_wc_conflict_choose_base:
                {
                  SVN_ERR(svn_wc__loggy_copy
                          (log_accum, NULL, adm_access,
                           svn_wc__copy_translate,
                           left, merge_target,
                           FALSE, pool));
                  *merge_outcome = svn_wc_merge_merged;
                  contains_conflicts = FALSE;
                  goto merge_complete;
                }
              case svn_wc_conflict_choose_theirs_full:
                {
                  SVN_ERR(svn_wc__loggy_copy
                          (log_accum, NULL, adm_access,
                           svn_wc__copy_translate,
                           right, merge_target,
                           FALSE, pool));
                  *merge_outcome = svn_wc_merge_merged;
                  contains_conflicts = FALSE;
                  goto merge_complete;
                }
                /* For a binary file, if the response is to use the
                   user's file, we do nothing.  We also do nothing if
                   the response claims to have already resolved the
                   problem.*/
              case svn_wc_conflict_choose_mine_full:
                {
                  *merge_outcome = svn_wc_merge_merged;
                  contains_conflicts = FALSE;
                  goto merge_complete;
                }
              case svn_wc_conflict_choose_merged:
                {
                  if (! result->merged_file)
                    {
                      /* Callback asked us to choose its own
                         merged file, but didn't provide one! */
                      return svn_error_create
                          (SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE,
                           NULL, _("Conflict callback violated API:"
                                   " returned no merged file"));
                    }
                  else
                    {
                      SVN_ERR(svn_wc__loggy_copy
                              (log_accum, NULL, adm_access,
                               svn_wc__copy_translate,
                               result->merged_file, merge_target,
                               FALSE, pool));
                      *merge_outcome = svn_wc_merge_merged;
                      contains_conflicts = FALSE;
                      goto merge_complete;
                    }
                }
              case svn_wc_conflict_choose_postpone:
              default:
                {
                  /* Assume conflict remains, fall through to code below. */
                }
            }
        }

      /* reserve names for backups of left and right fulltexts */
      SVN_ERR(svn_io_open_unique_file2(NULL,
                                       &left_copy,
                                       merge_target,
                                       left_label,
                                       svn_io_file_del_none,
                                       pool));

      SVN_ERR(svn_io_open_unique_file2(NULL,
                                       &right_copy,
                                       merge_target,
                                       right_label,
                                       svn_io_file_del_none,
                                       pool));

      /* create the backup files */
      SVN_ERR(svn_io_copy_file(left,
                               left_copy, TRUE, pool));
      SVN_ERR(svn_io_copy_file(right,
                               right_copy, TRUE, pool));

      /* Was the merge target detranslated? */
      if (merge_target != tmp_target)
        {
          /* Create a .mine file too */
          const char *mine_copy;

          SVN_ERR(svn_io_open_unique_file2(NULL,
                                           &mine_copy,
                                           merge_target,
                                           target_label,
                                           svn_io_file_del_none,
                                           pool));
          SVN_ERR(svn_wc__loggy_move(log_accum, NULL,
                                     adm_access,
                                     tmp_target,
                                     mine_copy,
                                     FALSE, pool));
          mine_copy = svn_path_is_child(adm_path, mine_copy, pool);
          tmp_entry.conflict_wrk = mine_copy;
        }
      else
        tmp_entry.conflict_wrk = NULL;

      /* Derive the basenames of the backup files. */
      svn_path_split(left_copy, &parentt, &left_base, pool);
      svn_path_split(right_copy, &parentt, &right_base, pool);

      /* Mark merge_target's entry as "Conflicted", and start tracking
         the backup files in the entry as well. */
      tmp_entry.conflict_old = left_base;
      tmp_entry.conflict_new = right_base;
      SVN_ERR(svn_wc__loggy_entry_modify
              (log_accum,
               adm_access, merge_target,
               &tmp_entry,
               SVN_WC__ENTRY_MODIFY_CONFLICT_OLD
               | SVN_WC__ENTRY_MODIFY_CONFLICT_NEW
               | SVN_WC__ENTRY_MODIFY_CONFLICT_WRK,
               pool));

      *merge_outcome = svn_wc_merge_conflict; /* a conflict happened */

    } /* end of binary conflict handling */
  else
    *merge_outcome = svn_wc_merge_conflict; /* dry_run for binary files. */

  merge_complete:
  /* Merging is complete.  Regardless of text or binariness, we might
     need to tweak the executable bit on the new working file.  */
  if (! dry_run)
    {
      SVN_ERR(svn_wc__loggy_maybe_set_executable(log_accum,
                                                 adm_access, merge_target,
                                                 pool));

      SVN_ERR(svn_wc__loggy_maybe_set_readonly(log_accum,
                                                adm_access, merge_target,
                                                pool));

    }

  return SVN_NO_ERROR;
}



svn_error_t *
svn_wc_merge3(enum svn_wc_merge_outcome_t *merge_outcome,
              const char *left,
              const char *right,
              const char *merge_target,
              svn_wc_adm_access_t *adm_access,
              const char *left_label,
              const char *right_label,
              const char *target_label,
              svn_boolean_t dry_run,
              const char *diff3_cmd,
              const apr_array_header_t *merge_options,
              const apr_array_header_t *prop_diff,
              svn_wc_conflict_resolver_func_t conflict_func,
              void *conflict_baton,
              apr_pool_t *pool)
{
  svn_stringbuf_t *log_accum = svn_stringbuf_create("", pool);

  SVN_ERR(svn_wc__merge_internal(&log_accum, merge_outcome,
                                 left, right, merge_target,
                                 NULL,
                                 adm_access,
                                 left_label, right_label, target_label,
                                 dry_run,
                                 diff3_cmd,
                                 merge_options,
                                 prop_diff,
                                 conflict_func, conflict_baton,
                                 pool));

  /* Write our accumulation of log entries into a log file */
  SVN_ERR(svn_wc__write_log(adm_access, 0, log_accum, pool));

  SVN_ERR(svn_wc__run_log(adm_access, NULL, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_merge2(enum svn_wc_merge_outcome_t *merge_outcome,
              const char *left,
              const char *right,
              const char *merge_target,
              svn_wc_adm_access_t *adm_access,
              const char *left_label,
              const char *right_label,
              const char *target_label,
              svn_boolean_t dry_run,
              const char *diff3_cmd,
              const apr_array_header_t *merge_options,
              apr_pool_t *pool)
{
  return svn_wc_merge3(merge_outcome,
                       left, right, merge_target, adm_access,
                       left_label, right_label, target_label,
                       dry_run, diff3_cmd, merge_options, NULL,
                       NULL, NULL, pool);
}

svn_error_t *
svn_wc_merge(const char *left,
             const char *right,
             const char *merge_target,
             svn_wc_adm_access_t *adm_access,
             const char *left_label,
             const char *right_label,
             const char *target_label,
             svn_boolean_t dry_run,
             enum svn_wc_merge_outcome_t *merge_outcome,
             const char *diff3_cmd,
             apr_pool_t *pool)
{
  return svn_wc_merge3(merge_outcome,
                       left, right, merge_target, adm_access,
                       left_label, right_label, target_label,
                       dry_run, diff3_cmd, NULL, NULL, NULL,
                       NULL, pool);
}



/* Constructor for the result-structure returned by conflict callbacks. */
svn_wc_conflict_result_t *
svn_wc_create_conflict_result(svn_wc_conflict_choice_t choice,
                              const char *merged_file,
                              apr_pool_t *pool)
{
  svn_wc_conflict_result_t *result = apr_pcalloc(pool, sizeof(*result));
  result->choice = choice;
  result->merged_file = merged_file;
  result->save_merged = FALSE;

  /* If we add more fields to svn_wc_conflict_result_t, add them here. */

  return result;
}
