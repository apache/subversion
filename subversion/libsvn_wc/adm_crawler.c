/*
 * adm_crawler.c:  report local WC mods to an Editor.
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

/* ==================================================================== */


#include <string.h>

#include "apr_pools.h"
#include "apr_file_io.h"
#include "apr_hash.h"

#include <assert.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_wc.h"
#include "svn_io.h"
#include "svn_sorts.h"
#include "svn_delta.h"

#include "wc.h"
#include "adm_files.h"
#include "props.h"
#include "translate.h"


/* Helper for report_revisions().
   
   Perform an atomic restoration of the file FILE_PATH; that is, copy
   the file's text-base to the administrative tmp area, and then move
   that file to FILE_PATH with possible translations/expansions.  */
static svn_error_t *
restore_file (svn_stringbuf_t *file_path,
              apr_pool_t *pool)
{
  svn_stringbuf_t *text_base_path, *tmp_text_base_path;
  svn_wc_keywords_t *keywords;
  enum svn_wc__eol_style eol_style;
  const char *eol;
  svn_boolean_t toggled;

  text_base_path = svn_wc__text_base_path (file_path, FALSE, pool);
  tmp_text_base_path = svn_wc__text_base_path (file_path, TRUE, pool);

  SVN_ERR (svn_io_copy_file (text_base_path->data, tmp_text_base_path->data,
                             FALSE, pool));

  SVN_ERR (svn_wc__get_eol_style (&eol_style, &eol,
                                  file_path->data, pool));
  SVN_ERR (svn_wc__get_keywords (&keywords,
                                 file_path->data, NULL, pool));
  
  /* When copying the tmp-text-base out to the working copy, make
     sure to do any eol translations or keyword substitutions,
     as dictated by the property values.  If these properties
     are turned off, then this is just a normal copy. */
  SVN_ERR (svn_wc_copy_and_translate (tmp_text_base_path->data,
                                      file_path->data,
                                      eol, FALSE, /* don't repair */
                                      keywords,
                                      TRUE, /* expand keywords */
                                      pool));
  
  SVN_ERR (svn_io_remove_file (tmp_text_base_path->data, pool));

  /* If necessary, tweak the new working file's executable bit. */
  SVN_ERR (svn_wc__maybe_toggle_working_executable_bit 
           (&toggled, file_path->data, pool));

  /* Remove any text conflict */
  SVN_ERR (svn_wc_resolve_conflict (file_path, TRUE, FALSE, NULL, NULL, pool));

  /* ### hey guys, shouldn't we recording the 'restored'
     working-file's timestamp in its entry?  Right now, every time we
     restore a file, the front-line-timestamp-check-for-modifiedness
     is being destroyed. */

  return SVN_NO_ERROR;
}


/* The recursive crawler that describes a mixed-revision working
   copy to an RA layer.  Used to initiate updates.

   This is a depth-first recursive walk of DIR_PATH under WC_PATH.
   Look at each entry and check if its revision is different than
   DIR_REV.  If so, report this fact to REPORTER.  If an entry is
   missing from disk, report its absence to REPORTER.  

   If RESTORE_FILES is set, then unexpectedly missing working files
   will be restored from text-base and NOTIFY_FUNC/NOTIFY_BATON
   will be called to report the restoration. */
static svn_error_t *
report_revisions (svn_stringbuf_t *wc_path,
                  svn_stringbuf_t *dir_path,
                  svn_revnum_t dir_rev,
                  const svn_ra_reporter_t *reporter,
                  void *report_baton,
                  svn_wc_notify_func_t notify_func,
                  void *notify_baton,
                  svn_boolean_t restore_files,
                  svn_boolean_t recurse,
                  apr_pool_t *pool)
{
  apr_hash_t *entries, *dirents;
  apr_hash_index_t *hi;
  apr_pool_t *subpool = svn_pool_create (pool);
  svn_wc_entry_t *dot_entry;
  svn_stringbuf_t *this_url, *this_path, *this_full_path;

  /* Construct the actual 'fullpath' = wc_path + dir_path */
  svn_stringbuf_t *full_path = svn_stringbuf_dup (wc_path, subpool);
  svn_path_add_component (full_path, dir_path);

  /* Get both the SVN Entries and the actual on-disk entries. */
  SVN_ERR (svn_wc_entries_read (&entries, full_path, subpool));
  SVN_ERR (svn_io_get_dirents (&dirents, full_path, subpool));
  
  /* Do the real reporting and recursing. */
  
  /* First, look at "this dir" to see what it's URL is. */
  dot_entry = apr_hash_get (entries, SVN_WC_ENTRY_THIS_DIR, 
                                 APR_HASH_KEY_STRING);
  this_url = svn_stringbuf_dup (dot_entry->url, pool);
  this_path = svn_stringbuf_dup (dir_path, subpool);
  this_full_path = svn_stringbuf_dup (full_path, subpool);

  /* Looping over current directory's SVN entries: */
  for (hi = apr_hash_first (subpool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      svn_wc_entry_t *current_entry; 
      enum svn_node_kind *dirent_kind;
      svn_boolean_t missing = FALSE;

      /* Get the next entry */
      apr_hash_this (hi, &key, &klen, &val);
      current_entry = (svn_wc_entry_t *) val;

      /* Compute the name of the entry.  Skip THIS_DIR altogether. */
      if (! strcmp (key, SVN_WC_ENTRY_THIS_DIR))
        continue;

      /* Compute the complete path of the entry, relative to dir_path,
         and the calculated URL of the entry.  */
      if (this_path->len > dir_path->len)
        svn_stringbuf_chop (this_path, this_path->len - dir_path->len);
      if (this_full_path->len > full_path->len)
        svn_stringbuf_chop (this_full_path, 
                            this_full_path->len - full_path->len);
      if (this_url->len > dot_entry->url->len)
        svn_stringbuf_chop (this_url, this_url->len - dot_entry->url->len);
      svn_path_add_component_nts (this_path, key);
      svn_path_add_component_nts (this_full_path, key);
      svn_path_add_component_nts (this_url, key);
      
      /* The Big Tests: */
      
      /* Is the entry on disk?  Set a flag if not. */
      dirent_kind = (enum svn_node_kind *) apr_hash_get (dirents, key, klen);
      if (! dirent_kind)
        missing = TRUE;
      
      /* From here on out, ignore any entry scheduled for addition
         or deletion */
      if (current_entry->schedule != svn_wc_schedule_normal)
        continue;

      /* The entry exists on disk, and isn't `deleted'. */
      if (current_entry->kind == svn_node_file) 
        {
          if (dirent_kind && (*dirent_kind != svn_node_file))
            {
              /* If the dirent changed kind, report it as missing and
                 move on to the next entry.  Later on, the update
                 editor will return an 'obstructed update' error.  :) */
              SVN_ERR (reporter->delete_path (report_baton,
                                              this_path->data));
              continue;
            }

          if (missing && restore_files)
            {
              /* Recreate file from text-base. */
              SVN_ERR (restore_file (this_full_path, pool));
              
              /* Report the restoration to the caller. */
              if (notify_func != NULL)
                (*notify_func) (notify_baton, 
                                svn_wc_notify_restore,
                                this_full_path->data);
            }

          /* Possibly report a disjoint URL... */
          if (! svn_stringbuf_compare (current_entry->url, this_url))
            SVN_ERR (reporter->link_path (report_baton,
                                          this_path->data,
                                          current_entry->url->data,
                                          current_entry->revision));
          /* ... or perhaps just a differing revision. */
          else if (current_entry->revision !=  dir_rev)
            SVN_ERR (reporter->set_path (report_baton,
                                         this_path->data,
                                         current_entry->revision));
        }
      
      else if (current_entry->kind == svn_node_dir && recurse)
        {
          svn_wc_entry_t *subdir_entry;

          if (missing)
            {
              /* We can't recreate dirs locally, so report as missing,
                 and move on to the next entry.  */
              SVN_ERR (reporter->delete_path (report_baton, this_path->data));
              continue;
            }
          
          if (dirent_kind && (*dirent_kind != svn_node_dir))
            /* No excuses here.  If the user changed a versioned
               directory into something else, the working copy is
               hosed.  It can't receive updates within this dir
               anymore.  Throw a real error. */
            return svn_error_createf
              (SVN_ERR_WC_OBSTRUCTED_UPDATE, 0, NULL, subpool,
               "The entry '%s' is no longer a directory,\n"
               "which prevents proper updates.\n"
               "Please remove this entry and try updating again.",
               this_path->data);
          
          /* We need to read the full entry of the directory from its
             own "this dir", if available. */
          SVN_ERR (svn_wc_entry (&subdir_entry, this_full_path, subpool));

          /* Possibly report a disjoint URL... */
          if (! svn_stringbuf_compare (subdir_entry->url, this_url))
            SVN_ERR (reporter->link_path (report_baton,
                                          this_path->data,
                                          subdir_entry->url->data,
                                          subdir_entry->revision));
          /* ... or perhaps just a differing revision. */
          else if (subdir_entry->revision != dir_rev)
            SVN_ERR (reporter->set_path (report_baton,
                                         this_path->data,
                                         subdir_entry->revision));

          /* Recurse. */
          SVN_ERR (report_revisions (wc_path, this_path,
                                     subdir_entry->revision,
                                     reporter, report_baton,
                                     notify_func, notify_baton,
                                     restore_files, recurse,
                                     subpool));
        } /* end directory case */

    } /* end main entries loop */

  /* We're done examining this dir's entries, so free everything. */
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}


/*------------------------------------------------------------------*/
/*** Public Interfaces ***/


/* This is the main driver of the working copy state "reporter", used
   for updates. */
svn_error_t *
svn_wc_crawl_revisions (svn_stringbuf_t *path,
                        const svn_ra_reporter_t *reporter,
                        void *report_baton,
                        svn_boolean_t restore_files,
                        svn_boolean_t recurse,
                        svn_wc_notify_func_t notify_func,
                        void *notify_baton,
                        apr_pool_t *pool)
{
  svn_error_t *err;
  svn_wc_entry_t *entry;
  svn_revnum_t base_rev = SVN_INVALID_REVNUM;
  svn_boolean_t missing = FALSE;

  /* The first thing we do is get the base_rev from the working copy's
     ROOT_DIRECTORY.  This is the first revnum that entries will be
     compared to. */
  SVN_ERR (svn_wc_entry (&entry, path, pool));
  base_rev = entry->revision;
  if (base_rev == SVN_INVALID_REVNUM)
    {
      svn_stringbuf_t *parent_name = svn_stringbuf_dup (path, pool);
      svn_wc_entry_t *parent_entry;
      svn_path_remove_component (parent_name);
      SVN_ERR (svn_wc_entry (&parent_entry, parent_name, pool));
      base_rev = parent_entry->revision;
    }

  /* The first call to the reporter merely informs it that the
     top-level directory being updated is at BASE_REV.  Its PATH
     argument is ignored. */
  SVN_ERR (reporter->set_path (report_baton, "", base_rev));

  if (entry->schedule != svn_wc_schedule_delete)
    {
      apr_finfo_t info;
      apr_status_t apr_err;
      apr_err = apr_stat (&info, path->data, APR_FINFO_MIN, pool);
      if (APR_STATUS_IS_ENOENT(apr_err))
        missing = TRUE;
    }

  if (entry->kind == svn_node_dir)
    {
      if (missing)
        {
          /* Always report directories as missing;  we can't recreate
             them locally. */
          err = reporter->delete_path (report_baton, "");
          if (err)
            {
              /* Clean up the fs transaction. */
              svn_error_t *fserr;
              fserr = reporter->abort_report (report_baton);
              if (fserr)
                return svn_error_quick_wrap (fserr, "Error aborting report.");
              else
                return err;
            }
        }

      else 
        {
          /* Recursively crawl ROOT_DIRECTORY and report differing
             revisions. */
          err = report_revisions (path,
                                  svn_stringbuf_create ("", pool),
                                  base_rev,
                                  reporter, report_baton,
                                  notify_func, notify_baton,
                                  restore_files, recurse, pool);
          if (err)
            {
              /* Clean up the fs transaction. */
              svn_error_t *fserr;
              fserr = reporter->abort_report (report_baton);
              if (fserr)
                return svn_error_quick_wrap (fserr, "Error aborting report.");
              else
                return err;
            }
        }
    }

  else if (entry->kind == svn_node_file)
    {
      if (missing && restore_files)
        {
          /* Recreate file from text-base. */
          SVN_ERR (restore_file (path, pool));

          /* Report the restoration to the caller. */
          if (notify_func != NULL)
            (*notify_func) (notify_baton, svn_wc_notify_restore, path->data);
        }

      if (entry->revision != base_rev)
        {
          /* If this entry is a file node, we just want to report that
             node's revision.  Since we are looking at the actual target
             of the report (not some file in a subdirectory of a target
             directory), and that target is a file, we need to pass an
             empty string to set_path. */
          err = reporter->set_path (report_baton, "", base_rev);
          if (err)
            {
              /* Clean up the fs transaction. */
              svn_error_t *fserr;
              fserr = reporter->abort_report (report_baton);
              if (fserr)
                return svn_error_quick_wrap (fserr, "Error aborting report.");
              else
                return err;
            }
        }
    }

  /* Finish the report, which causes the update editor to be driven. */
  err = reporter->finish_report (report_baton);
  if (err)
    {
      /* Clean up the fs transaction. */
      svn_error_t *fserr;
      fserr = reporter->abort_report (report_baton);
      if (fserr)
        return svn_error_quick_wrap (fserr, "Error aborting report.");
      else
        return err;
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_transmit_text_deltas (svn_stringbuf_t *path,
                             svn_boolean_t fulltext,
                             const svn_delta_editor_t *editor,
                             void *file_baton,
                             svn_stringbuf_t **tempfile,
                             apr_pool_t *pool)
{
  svn_stringbuf_t *tmpf, *tmp_base;
  apr_status_t status;
  svn_txdelta_window_handler_t handler;
  void *wh_baton;
  svn_txdelta_stream_t *txdelta_stream;
  apr_file_t *localfile = NULL;
  apr_file_t *basefile = NULL;
  
  /* Tell the editor that we're about to apply a textdelta to the
     file baton; the editor returns to us a window consumer routine
     and baton.  If there is no handler provided, just close the file
     and get outta here.  */
  SVN_ERR (editor->apply_textdelta (file_baton, &handler, &wh_baton));
  if (! handler)
    return editor->close_file (file_baton);

  /* Make an untranslated copy of the working file in the
     adminstrative tmp area because a) we want this to work even if
     someone changes the working file while we're generating the
     txdelta, b) we need to detranslate eol and keywords anyway, and
     c) after the commit, we're going to copy the tmp file to become
     the new text base anyway.

     Note that since the translation routine doesn't let you choose
     the filename, we have to do one extra copy.  But what the heck,
     we're about to generate an svndiff anyway. */
  SVN_ERR (svn_wc_translated_file (&tmpf, path, pool));
  tmp_base = svn_wc__text_base_path (path, TRUE, pool);
  SVN_ERR (svn_io_copy_file (tmpf->data, tmp_base->data, FALSE, pool));

  /* Alert the caller that we have created a temporary file that might
     need to be cleaned up. */
  if (tempfile)
    *tempfile = tmp_base;

  /* If the translation step above actually created a new file, delete
     the old one. */
  if (tmpf != path)
    SVN_ERR (svn_io_remove_file (tmpf->data, pool));
      
  /* If we're not sending fulltext, we'll be sending diffs against the
     text-base. */
  if (! fulltext)
    {
      /* Before we set up an svndiff stream against the old text base,
         make sure the old text base still matches its checksum.
         Otherwise we could send corrupt data and never know it. */ 

      svn_stringbuf_t *checksum;
      svn_stringbuf_t *tb = svn_wc__text_base_path (path, FALSE, pool);
      svn_wc_entry_t *ent;
      
      SVN_ERR (svn_wc_entry (&ent, path, pool));
      SVN_ERR (svn_io_file_checksum (&checksum, tb->data, pool));
      
      /* For backwards compatibility, no checksum means assume a match. */
      if (ent->checksum && (! svn_stringbuf_compare (checksum, ent->checksum)))
        {
          /* There is an entry checksum, but it does not match the
             actual text base checksum.  Extreme badness.  Of course,
             theoretically we could just switch to fulltext
             transmission here, and everything would work fine; after
             all, we're going to replace the text base with a new one
             in a moment anyway, and we'd fix the checksum then.
             But it's better to error out.  People should know that
             their text bases are getting corrupted, so they can
             investigate.  Other commands could be affected, too, such
             as `svn diff'.  */
          
          /* Deliberately ignore error here; the error about the
             checksum mismatch is more important to return. */
          svn_io_remove_file (tmp_base->data, pool);
          
          if (tempfile)
            *tempfile = NULL;
          
          return svn_error_createf
            (SVN_ERR_WC_CORRUPT_TEXT_BASE, 0, NULL, pool,
             "svn_wc_transmit_text_deltas: checksum mismatch for '%s':\n"
             "   recorded checksum: %s\n"
             "   actual checksum:   %s\n",
             tb->data, ent->checksum->data, checksum->data);
        }
      else
        SVN_ERR (svn_wc__open_text_base (&basefile, path, APR_READ, pool));
    }

  /* Open a filehandle for tmp text-base. */
  if ((status = apr_file_open (&localfile, tmp_base->data, 
                               APR_READ, APR_OS_DEFAULT, pool)))
    {
      return svn_error_createf (status, 0, NULL, pool,
                                "do_apply_textdelta: error opening '%s'",
                                tmp_base->data);
    }

  /* Create a text-delta stream object that pulls data out of the two
     files. */
  svn_txdelta (&txdelta_stream,
               svn_stream_from_aprfile (basefile, pool),
               svn_stream_from_aprfile (localfile, pool),
               pool);
  
  /* Pull windows from the delta stream and feed to the consumer. */
  SVN_ERR (svn_txdelta_send_txstream (txdelta_stream, handler, 
                                      wh_baton, pool));
    
  /* Close the two files */
  if ((status = apr_file_close (localfile)))
    return svn_error_create (status, 0, NULL, pool,
                             "error closing local file");
  
  if (basefile)
    SVN_ERR (svn_wc__close_text_base (basefile, path, 0, pool));

  /* Close the file baton, and get outta here. */
  return editor->close_file (file_baton);
}


svn_error_t *
svn_wc_transmit_prop_deltas (svn_stringbuf_t *path,
                             svn_node_kind_t kind,
                             const svn_delta_editor_t *editor,
                             void *baton,
                             svn_stringbuf_t **tempfile,
                             apr_pool_t *pool)
{
  int i;
  svn_stringbuf_t *props, *props_base, *props_tmp;
  apr_array_header_t *propmods;
  apr_hash_t *localprops = apr_hash_make (pool);
  apr_hash_t *baseprops = apr_hash_make (pool);
  
  /* First, get the prop_path from the original path */
  SVN_ERR (svn_wc__prop_path (&props, path, 0, pool));
  
  /* Get the full path of the prop-base `pristine' file */
  SVN_ERR (svn_wc__prop_base_path (&props_base, path, 0, pool));

  /* Copy the local prop file to the administrative temp area */
  SVN_ERR (svn_wc__prop_path (&props_tmp, path, 1, pool));
  SVN_ERR (svn_io_copy_file (props->data, props_tmp->data, FALSE, pool));

  /* Alert the caller that we have created a temporary file that might
     need to be cleaned up. */
  if (tempfile)
    *tempfile = props_tmp;

  /* Load all properties into hashes */
  SVN_ERR (svn_wc__load_prop_file (props_tmp->data, localprops, pool));
  SVN_ERR (svn_wc__load_prop_file (props_base->data, baseprops, pool));
  
  /* Get an array of local changes by comparing the hashes. */
  SVN_ERR (svn_wc_get_local_propchanges (&propmods, localprops, 
                                         baseprops, pool));

  /* Apply each local change to the baton */
  for (i = 0; i < propmods->nelts; i++)
    {
      const svn_prop_t *p = &APR_ARRAY_IDX (propmods, i, svn_prop_t);
      if (kind == svn_node_file)
        SVN_ERR (editor->change_file_prop (baton, p->name, p->value, pool));
      else
        SVN_ERR (editor->change_dir_prop (baton, p->name, p->value, pool));
    }

  return SVN_NO_ERROR;
}




/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: */

