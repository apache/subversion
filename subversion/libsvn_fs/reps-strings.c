/* reps-strings.c : intepreting representations w.r.t. strings
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

#include <string.h>
#include <assert.h>
#include <apr_md5.h>

#include "db.h"
#include "svn_fs.h"
#include "svn_pools.h"
#include "fs.h"
#include "err.h"
#include "dbt.h"
#include "trail.h"
#include "reps-table.h"
#include "strings-table.h"
#include "reps-strings.h"



/*** Local prototypes. ***/

static svn_error_t *rep_read_range (svn_fs_t *fs,
                                    const char *rep_key,
                                    char *buf,
                                    apr_size_t offset,
                                    apr_size_t *len,
                                    trail_t *trail);



/*** Helper Functions ***/


/* Is representation a `fulltext' type?  */
static int
rep_is_fulltext (skel_t *rep)
{
  return svn_fs__matches_atom (rep->children->children, "fulltext");
}


/* Is representation a `delta' type?  */
static int
rep_is_delta (skel_t *rep)
{
  return svn_fs__matches_atom (rep->children->children, "delta");
}


static int
rep_is_mutable (skel_t *rep)
{
  /* The node "header" is the first element of a rep skel. */
  skel_t *header = rep->children;
  
  /* The 2nd element of the header, IF it exists, is the header's
     first `flag'.  It could be NULL.  */
  skel_t *flag = header->children->next;
  
  while (flag)
    {
      if (svn_fs__matches_atom (flag, "mutable"))
        return TRUE;

      flag = flag->next;
    }
  
  /* Reached the end of the header skel, no mutable flag was found. */
  return FALSE;
}


/* Add the "mutable" flag to representation REP.  Allocate the flag in
   POOL; it is advisable that POOL be at least as long-lived as the
   pool REP is allocated in.  If the mutability flag is already set,
   this function does nothing.  */
static void
rep_set_mutable_flag (skel_t *rep, apr_pool_t *pool)
{
  if (! rep_is_mutable (rep))
    svn_fs__append (svn_fs__str_atom ("mutable", pool), rep->children);
    
  return;
}


/* Return a `fulltext' rep skel which references the string STR_KEY,
   performing allocations in POOL.  If MUTABLE is non-zero, make the
   representation mutable.  If non-NULL, STR_KEY will be copied into
   an allocation of POOL.  */
static skel_t *
make_fulltext_rep_skel (const char *str_key, 
                        int mutable,
                        apr_pool_t *pool)

{
  skel_t *rep_skel = svn_fs__make_empty_list (pool);
  skel_t *header = svn_fs__make_empty_list (pool);

  if (mutable)
    svn_fs__prepend (svn_fs__str_atom ("mutable", pool), header);

  svn_fs__prepend (svn_fs__str_atom ("fulltext", pool), header);
  svn_fs__prepend (str_key ? svn_fs__str_atom (str_key, pool)
                           : svn_fs__mem_atom (0, 0, pool), rep_skel);
  svn_fs__prepend (header, rep_skel);

  return rep_skel;
}


/* Set *KEY to the string key pointed to by REP, allocating the key in
   POOL.  REP is a `fulltext' rep. */
static svn_error_t *
fulltext_string_key (const char **key, skel_t *rep, apr_pool_t *pool)
{
  if (! rep_is_fulltext (rep))
    return svn_error_create 
      (SVN_ERR_FS_GENERAL, 0, NULL, pool,
       "fulltext_string_key: representation is not of type `fulltext'");

  *key = apr_pstrndup (pool, 
                       rep->children->next->data,
                       rep->children->next->len);
  return SVN_NO_ERROR;
}


/* Set *KEYS to an array of string keys gleaned from `delta'
   representation REP.  Allocate *KEYS in POOL. */
static svn_error_t *
delta_string_keys (apr_array_header_t **keys, skel_t *rep, apr_pool_t *pool)
{
  const char *key;
  skel_t *window = rep->children->next;
  int num_windows = 0;

  if (! rep_is_delta (rep))
    return svn_error_create 
      (SVN_ERR_FS_GENERAL, 0, NULL, pool,
       "delta_string_key: representation is not of type `delta'");

  /* Count the number of windows */
  while (window)
    {
      num_windows++;
      window = window->next;
    }

  /* Now, push the string keys for each window into *KEYS */
  *keys = apr_array_make (pool, num_windows, sizeof (key));
  window = rep->children->next;
  while (window)
    {
      skel_t *diff = window->children->next->children->children;
      
      if (strncmp ("svndiff", diff->data, diff->len) != 0)
        return svn_error_create
          (SVN_ERR_FS_CORRUPT, 0, NULL, pool,
           "string_key: delta rep uses unknown diff format");
          
      key = apr_pstrndup (pool, diff->next->data, diff->next->len);
      (*((const char **)(apr_array_push (*keys)))) = key;
      window = window->next;
    }

  return SVN_NO_ERROR;
}


/* Delete the strings associated with array KEYS in FS as part of TRAIL.  */
static svn_error_t *
delete_strings (apr_array_header_t *keys, 
                svn_fs_t *fs, 
                trail_t *trail)
{
  int i;
  const char *str_key;

  for (i = 0; i < keys->nelts; i++)
    {
      str_key = ((const char **)(keys->elts))[i];
      SVN_ERR (svn_fs__string_delete (fs, str_key, trail));
    }
  return SVN_NO_ERROR;
}



/*** Reading the contents from a representation. ***/

/* The fulltext reconstruction code has its weak spot isolated to one
 * case in the function window_handler().  By improving that case, we
 * asymptotically approach having a real delta combiner; for now, it's
 * just the naive reconstruction method.
 *
 * Here's an overview:
 *
 * rep_read_range() runs through the raw svndiff data, passing it into
 * a stream which invokes window_handler() every time a new window is
 * available.  The window_handler() ignores windows until it sees one
 * that reconstructs data within the range requested, at which point
 * it
 *
 *     1. obtains the range of source fulltext used by this window in
 *        reconstructing whatever portion the requested target range,
 *        by naively making a recursive call to rep_read_range(),
 *
 *   or
 *
 *     2. looks at the source rep; if it's a fulltext, does a dance
 *        for joy and grabs the relevant range, else if it's a delta,
 *        starts reading windows and reconstructs on the fly --
 *        wherever, this new window stream itself needs source data,
 *        it starts reading windows, and so on...
 *
 * [Got this up and running using #1, next task is to switch to #2.]
 *
 * When window_handler() has finished reconstructing the requested
 * range, or receives the null window, it sets the `done' bit in its
 * baton, so that rep_read_range() won't bother looping over the
 * trailing svndiff data.
 * 
 * We won't bother to evaluate plan #1; its weaknesses are well-known,
 * although it'll probably perform acceptably for a while.
 *
 * Let's assume we've finished implementing plan #2.  How does it
 * perform?  In terms of number of passes over ignored leading svndiff
 * data, its worst case is probably Subversion's most common case,
 * that is, looping to read a whole file from beginning to end.  But
 * note that the case is only bad when each loop reads a chunk that is
 * small relative to the full size of the file.  If you use big
 * chunks, the penalty is not so bad; and if you read the whole file
 * in one call, then there's no penalty at all (except, of course,
 * that you held a whole file in memory, which your mother taught you
 * never to do).  Users of the FS interface should use their
 * judgement; probably it would be good to read a whole file at a time
 * on checkouts, for example, except when a file is really
 * prohibitively large.
 */


/* Baton for window_handler() below.  Note that this baton can live
   across multiple calls to window_handler(). */
struct window_handler_baton_t
{
  /* Where to store the data as we undeltify it. */
  char *buf;

  /* Requested offset into the fulltext. */
  apr_size_t req_offset;

  /* Current offset into the fulltext. */
  apr_size_t cur_offset;

  /* The FS in which `base_rep' can be found. */
  svn_fs_t *fs;

  /* Representation whose fulltext this delta was made against. */
  const char *base_rep;

  /* Amount of fulltext requested to reconstruct. */
  apr_size_t len_req;

  /* Amount of fulltext reconstructed so far;
     i.e., the offset into buf. */
  apr_size_t len_read;

  /* False until we have received the null (final) window. */
  svn_boolean_t done;

  /* Trail in which to do everything. */
  trail_t *trail;

  /* Pool in which to do temporary allocations.  This may be cleared
     by the window handler, so you probably don't want it to be the
     pool in which this baton or its buf live. */
  apr_pool_t *pool;
};


/* Function of type `svn_txdelta_window_handler_t';
   BATON is a `struct window_handler_baton_t'.

   If BATON->done is set, do nothing and return immediately.
   Otherwise...

   If WINDOW is relevant, reconstruct some portion of BATON->buf, as
   part of BATON->trail; any temporary allocation happens in
   BATON->pool, which may be cleared before the handler exits.  If
   WINDOW is irrelevant, ignore it and return.

   Q: When is WINDOW irrelevant?

   A: If the range (BATON->req_offset + BATON->len_req) does not
      overlap with the range (WINDOW->tview_len + BATON->cur_offset),
      then the window is irrelevant, so: If the former range lies
      before the latter, then increment BATON->cur_offset by
      WINDOW->tview_len, else if the former range lies after the
      latter, set BATON->done to 1, and then return in either case.

      If the ranges do overlap, then the window is relevant -- that
      is, it reconstructs some or all of the requested content range,
      BATON->req_offset + BATON->len_req, so read on...

   Q: Okay, so what exactly happens when WINDOW is relevant?

   A: In that case, the data reconstructed by this window is stored at
      BATON->buf + BATON->len_read, and BATON->len_read is incremented
      by the number of bytes reconstructed, and BATON->cur_offset is
      incremented by the same amount.

      BATON->base_rep may be used to obtain source text against which
      to reconstruct.  
      
      ### todo:  I'll go into shock if this function doesn't change as
      a result of the new `delta' representation scheme.
*/ 
static svn_error_t *
window_handler (svn_txdelta_window_t *window, void *baton)
{
  struct window_handler_baton_t *wb = baton;

  /* If we're done, we're done. */
  if ((window == NULL) || wb->done)
    {
      wb->done = TRUE;  /* might be redundant */
      return SVN_NO_ERROR;
    }

  /** Otherwise, handle the window. **/
  
  /* Get the range of source text that's relevant to us. */

  /* ### todo: if we wanted to make the naive algorithm really space
     efficient, we could pass in (wb->buf + some_offset) for the data
     buffer in a bunch of tiny calls to rep_read_range(), and
     reconstruct the data in-place.  That would probably be, ahem,
     slow. :-)  And anyway, we're going to do things differently. */

  {
    char *tbuf;       /* Reconstructed target data. */
    char *sbuf;       /* Reconstructed source data. */
    apr_size_t slen;  /* Length of source data. */

    slen = window->sview_len;
    sbuf = apr_palloc (wb->pool, slen);
    tbuf = apr_palloc (wb->pool, window->tview_len);

    /* Q: Why is there a target buf allocated inside this function?
          Why not just use the output buffer directly?

       A: A given window (say, the current one) contains all the data
          necessary to reproduce a contiguous range of bytes.  If that
          range of bytes is entirely outside the range the caller
          requested, the window is ignored.  If, however, any overlap
          occurs between the window's "target view" and the requested
          range, this window must be processed.  When considering
          overlap, we have exactly one of the following situations:

             1. target window and requested range have the same
                starting offset.

             2. target window starting offset is greater than the
                starting offset of the requested range.

             3. target window starting offset is less than the
                starting offset of the requested range.

          Case 1 and Case 2 and fairly simple to deal with.  Case 1 is
          super-trivial.  Case 2 can be treated like a special Case 1
          because the "overlapped" portion came from a previous window(s).

          Case 3 is the weird one.  In this case, we have some amount
          of data coming out of the window op handling that needs to
          be discarded before we actually get to data that we care
          about.  Now, one might be tempted to just literally discard
          that data, and then actually begin writing to the output
          buffer at the proper time.  This would be fine if the only
          op types were svn_txdelta_source (which reads from a source
          buffer) and svn_txdelta_new (which reads from a "new data"
          buffer).  But the svn_txdelta_target op also exists, and it
          reads from the target buffer.  With this op type comes the
          risk that we will be asked to read from an offset that
          exists in the "overlap" region -- which we just discarded!

          So, in order to safeguard against the svn_txdelta_target op
          making requests for data which we no longer have, we need to
          "play out" this window into a temporary buffer, then copy
          the range requested by the caller into the output buffer
          once we're finished.
    */


    /* Now we can loop over the window ops, doing them.  I think this
       makes more sense than trying to use the functions in
       svn_delta.h.  We'd spend a lot of effort packing things up
       right, for not much gain. */

    {
      const svn_txdelta_op_t *op;
      int i;
      int src_read = 0;
      apr_size_t len_read = 0;
      apr_size_t copy_amt = 0;
      apr_size_t discard_amt = (wb->req_offset > wb->cur_offset) 
                                ? (wb->req_offset - wb->cur_offset)
                                : 0;

      /* For each op, we must check to see what portion of that op's output
         is meant for the "discard pile."  */
      for (i = 0; i < window->num_ops; i++)
        {
          op = window->ops + i;
          switch (op->action_code)
            {
            case svn_txdelta_source:
              {
                if (! src_read)
                  {
                    /* The first time we actually have a reference to
                       our source data, we'll read all the source data
                       that this window might use.  We may end up
                       reading more than we need to based on the
                       portion of this window our caller actually
                       requested, but we'll at least only have the one
                       database access in this window_handler() call.

                       ### todo: this is the core of the naive
                       algorithm, and is what has to go when we have a
                       true delta combiner.  */
                    SVN_ERR (rep_read_range (wb->fs, wb->base_rep, sbuf,
                                             window->sview_offset, &slen, 
                                             wb->trail));
                    src_read = 1;
                  }
                memcpy (tbuf + len_read, sbuf + op->offset, op->length);
                len_read += op->length;
              }
              break;

            case svn_txdelta_target:
              {
                /* This could be done in bigger blocks, at the expense
                   of some more complexity. */
                int t;
                for (t = op->offset; t < op->offset + op->length; t++)
                  tbuf[len_read++] = tbuf[t];
              }
              break;

            case svn_txdelta_new:
              {
                memcpy (tbuf + len_read,
                        window->new_data->data + op->offset,
                        op->length);
                len_read += op->length;
              }
              break;

            default:
              return svn_error_createf
                (SVN_ERR_FS_CORRUPT, 0, NULL, wb->pool,
                 "window_handler: unknown delta op action code (%d)",
                 op->action_code);
            }

          /* If we've at least read into the caller's requested range
             of data, figure out how much data we would copy into the
             output buffer were we going to do so right now.  If that
             amount is enough to "fill the request", stop handling ops
             here. */
          if (len_read >= discard_amt)
            {
              copy_amt = len_read - discard_amt;
              if (copy_amt > (wb->len_req - wb->len_read))
                {
                  copy_amt = wb->len_req - wb->len_read;
                  break;
                }
            }
        }

      /* Copy our requested range into the output buffer. */
      memcpy (wb->buf + wb->len_read, tbuf + discard_amt, copy_amt);
      wb->len_read += copy_amt;

      /* If this window looks past relevant data, then we're done. */
      wb->cur_offset += copy_amt;
      if (wb->cur_offset >= (wb->req_offset + wb->len_req))
        wb->done = TRUE;

    }
  }
    
  /* Clear out the window baton's pool. */
  svn_pool_clear (wb->pool);

  return SVN_NO_ERROR;
}


/* Copy into BUF *LEN bytes starting at OFFSET from the string
   represented via REP_KEY in FS, as part of TRAIL.
   The number of bytes actually copied is stored in *LEN.  */
static svn_error_t *
rep_read_range (svn_fs_t *fs,
                const char *rep_key,
                char *buf,
                apr_size_t offset,
                apr_size_t *len,
                trail_t *trail)
{
  skel_t *rep;
  apr_pool_t *subpool = svn_pool_create (trail->pool);

  /* Read in our REP. */
  SVN_ERR (svn_fs__read_rep (&rep, fs, rep_key, trail));
  if (rep_is_fulltext (rep))
    {
      /* Get the string key associated with REP, and read the
         requested range from that string. */
      const char *str_key;
      SVN_ERR (fulltext_string_key (&str_key, rep, subpool));
      SVN_ERR (svn_fs__string_read (fs, str_key, buf, offset, len, trail));
    }
  else
    {
      svn_stream_t *wstream;
      struct window_handler_baton_t wb;
      skel_t *this_window;
      char chunk[4096];      /* chunk of svndiff data */
      apr_size_t off;        /* offset into svndiff data */
      apr_size_t amt;        /* how much svndiff data to/was read */

      /* Initialize THIS_WINDOW to the first (OFFSET WINDOW) skel. */
      this_window = rep->children->next;
      assert (this_window != NULL);

      /* Initialize the window handler baton. */
      wb.fs            = fs;
      wb.buf           = buf;
      wb.req_offset    = offset;
      wb.len_req       = *len;
      wb.len_read      = 0;
      wb.done          = FALSE;
      wb.trail         = trail;
      wb.pool          = svn_pool_create (subpool);

      /* Set up a window handling stream for the svndiff data. */
      wstream = svn_txdelta_parse_svndiff (window_handler, &wb, 
                                           FALSE, subpool);

      /* First things first:  send the "SVN\0" header through the
         stream. */
      chunk[0] = 'S';
      chunk[1] = 'V';
      chunk[2] = 'N';
      chunk[3] = '\0';
      amt = 4;
      SVN_ERR (svn_stream_write (wstream, chunk, &amt));
      
      /* Now, for each window, decide if the window is relevant.  That
         is, do we need to use to reconstruct data in the range
         requested by the caller?  */
      do
        {
          skel_t *wnd_skel = this_window->children->next;
          apr_size_t this_off, this_len;
          const char *str_key;

          /* Get the offset and size of this window from the skel. */
          this_off = atoi (apr_pstrndup (subpool,
                                         this_window->children->data,
                                         this_window->children->len));
          this_len = atoi (apr_pstrndup (subpool,
                                         wnd_skel->children->next->data,
                                         wnd_skel->children->next->len));

          /* If this window is irrelevant because it reconstructs text
             that is entirely before the range we're interested in,
             then ignore it. */
          if ((this_off + this_len - 1) < offset)
            {
              this_window = this_window->next;
              continue;
            }

          /* If this window is irrelevant because it reconstructs text
             that is entirely after the range we're interested in,
             we're definitely done. */
          if (this_off > (offset + *len))
            break;

          /* Get this string key which holds this window's data. 
             ### todo: make sure this is an `svndiff' DIFF skel here. */
          str_key = apr_pstrndup (subpool,
                                  wnd_skel->children->children->next->data,
                                  wnd_skel->children->children->next->len);

          /* Finish initializing our baton with window-specific
             stuff. */
          wb.cur_offset = this_off;
          wb.base_rep = 
            apr_pstrndup (subpool,
                          wnd_skel->children->next->next->next->data,
                          wnd_skel->children->next->next->next->len);

          /* Run through the svndiff data, at least as far as necessary. */
          off = 0;
          do {
            amt = sizeof (chunk);
            SVN_ERR (svn_fs__string_read (fs, str_key, chunk, 
                                          off, &amt, trail));
            off += amt;
            SVN_ERR (svn_stream_write (wstream, chunk, &amt));

          } while ((wb.done == FALSE) && (amt != 0));

          this_window = this_window->next;
        }
      while ((this_window) && (wb.done == FALSE));

      /* Close the stream.  We should not get an error for closing the
         stream early because we explicitly told the stream handlers
         not to care in the call to svn_txdelta_parse_svndiff() above.  */
      SVN_ERR (svn_stream_close (wstream));

      *len = wb.len_read;
    }

  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__get_mutable_rep (const char **new_rep,
                         const char *rep,
                         svn_fs_t *fs, 
                         trail_t *trail)
{
  skel_t *rep_skel;

  if (rep && (rep[0] != '\0'))
    {
      /* We were passed an existing rep, so examine it. */
      SVN_ERR (svn_fs__read_rep (&rep_skel, fs, rep, trail));

      if (rep_is_mutable (rep_skel))  /* rep already mutable, so return it */
        {
          *new_rep = rep;
          return SVN_NO_ERROR;
        }

      /* If REP is not mutable, we have to make a mutable copy.  It is
         a deep copy -- we copy the immutable rep's data.  Note that
         we copy it as fulltext, no matter how the immutable rep
         represents the data.  */
      if (rep_is_fulltext (rep_skel))
        {
          /* The easy case -- copy the fulltext string directly. */
          const char *old_str, *new_str;

          /* Step 1:  Copy the string to which the rep refers. */
          SVN_ERR (fulltext_string_key (&old_str, rep_skel, trail->pool));
          SVN_ERR (svn_fs__string_copy (fs, &new_str, old_str, trail));
          
          /* Step 2:  Make this rep mutable. */
          rep_set_mutable_flag (rep_skel, trail->pool);
          
          /* Step 3:  Change the string key to which this rep points. */
          rep_skel->children->next->data = new_str;
          rep_skel->children->next->len = strlen (new_str);
        }
      else
        {
          /* This is a bit trickier.  The immutable rep is a delta,
             but we're still making a fulltext copy of it.  So we do
             an undeltifying read loop, writing the fulltext out to
             the mutable rep.  The efficiency of this depends on the
             efficiency of rep_read_range(); fortunately, this
             circumstance is probably rare, and especially unlikely to
             happen on large contents (i.e., it's more likely to
             happen on directories than on files, because directories
             don't have to be up-to-date to receive commits, whereas
             files do.  */

          char buf[10000];
          apr_size_t offset;
          apr_size_t size;
          const char *new_str = NULL;
          apr_size_t amount;
          
          SVN_ERR (svn_fs__rep_contents_size (&size, fs, rep, trail));
          
          for (offset = 0; offset < size; offset += amount)
            {
              if ((size - offset) > (sizeof (buf)))
                amount = sizeof (buf);
              else
                amount = size - offset;
              
              SVN_ERR (rep_read_range (fs, rep, buf, offset, &amount, trail));
              SVN_ERR (svn_fs__string_append (fs, &new_str, amount, buf,
                                              trail));
            }
          
          rep_skel = make_fulltext_rep_skel (new_str, 1, trail->pool);
        }
    }
  else    /* no key, so make a new, empty, mutable, fulltext rep */
    {
      const char *new_str = NULL;
      SVN_ERR (svn_fs__string_append (fs, &new_str, 0, NULL, trail));
      rep_skel = make_fulltext_rep_skel (new_str, 1, trail->pool);
    }

  /* If we made it here, there's a new rep to store in the fs. */
  SVN_ERR (svn_fs__write_new_rep (new_rep, fs, rep_skel, trail));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__make_rep_immutable (svn_fs_t *fs,
                            const char *rep,
                            trail_t *trail)
{
  skel_t *rep_skel;
  skel_t *header, *flag, *prev;

  SVN_ERR (svn_fs__read_rep (&rep_skel, fs, rep, trail));
  header = rep_skel->children;

  /* The flags start at the 2nd element of the header. */
  for (flag = header->children->next, prev = NULL;
       flag;
       prev = flag, flag = flag->next)
    {
      if (flag->is_atom && svn_fs__matches_atom (flag, "mutable"))
        {
          /* We found it.  */
          if (prev)
            prev->next = flag->next;
          else
            header->children->next = NULL;
          
          SVN_ERR (svn_fs__write_rep (fs, rep, rep_skel, trail));
          break;
        }
    }
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__delete_rep_if_mutable (svn_fs_t *fs,
                               const char *rep,
                               trail_t *trail)
{
  skel_t *rep_skel;
  const char *str_key;

  SVN_ERR (svn_fs__read_rep (&rep_skel, fs, rep, trail));
  if (! rep_is_mutable (rep_skel))
    return SVN_NO_ERROR;

  if (rep_is_fulltext (rep_skel))
    {
      SVN_ERR (fulltext_string_key (&str_key, rep_skel, trail->pool));
      SVN_ERR (svn_fs__string_delete (fs, str_key, trail));
    }
  else /* delta */
    {
      apr_array_header_t *keys;
      SVN_ERR (delta_string_keys (&keys, rep_skel, trail->pool));
      SVN_ERR (delete_strings (keys, fs, trail));
    }

  SVN_ERR (svn_fs__delete_rep (fs, rep, trail));
  return SVN_NO_ERROR;
}



/*** Reading and writing data via representations. ***/

/** Reading. **/

struct rep_read_baton
{
  /* The FS from which we're reading. */
  svn_fs_t *fs;

  /* The representation skel whose contents we want to read.  If this
     is null, the rep has never had any contents, so all reads fetch 0
     bytes.

     Formerly, we cached the entire rep skel here, not just the key.
     That way we didn't have to fetch the rep from the db every time
     we want to read a little bit more of the file.  Unfortunately,
     this has a problem: if, say, a file's representation changes
     while we're reading (changes from fulltext to delta, for
     example), we'll never know it.  So for correctness, we now
     refetch the representation skel every time we want to read
     another chunk.  */
  const char *rep_key;
  
  /* How many bytes have been read already. */
  apr_size_t offset;

  /* If present, the read will be done as part of this trail, and the
     trail's pool will be used.  Otherwise, see `pool' below.  */
  trail_t *trail;

  /* Used for temporary allocations, iff `trail' (above) is null.  */
  apr_pool_t *pool;

};


static struct rep_read_baton *
rep_read_get_baton (svn_fs_t *fs,
                            const char *rep_key,
                            apr_size_t offset,
                            trail_t *trail,
                            apr_pool_t *pool)
{
  struct rep_read_baton *b;

  b = apr_pcalloc (pool, sizeof (*b));
  b->fs = fs;
  b->trail = trail;
  b->pool = pool;
  b->rep_key = rep_key;
  b->offset = offset;

  return b;
}



/*** Retrieving data. ***/

svn_error_t *
svn_fs__rep_contents_size (apr_size_t *size_p,
                           svn_fs_t *fs,
                           const char *rep,
                           trail_t *trail)
{
  skel_t *rep_skel;

  SVN_ERR (svn_fs__read_rep (&rep_skel, fs, rep, trail));

  if (rep_is_fulltext (rep_skel))
    {
      /* Get the size by asking Berkeley for the string's length. */
      const char *str_key;
      SVN_ERR (fulltext_string_key (&str_key, rep_skel, trail->pool));
      SVN_ERR (svn_fs__string_size (size_p, fs, str_key, trail));
    }
  else  /* rep is delta */
    {
      /* Get the size by finding the last window pkg in the delta and
         adding its offset to its size.  This way, we won't even be
         messed up by overlapping windows, as long as the window pkgs
         are still ordered. */

      skel_t *pkg_skel = rep_skel->children->next;
      skel_t *window_skel;
      char *offset_str, *size_str;
      int offset, size;

      assert (pkg_skel != NULL);

      /* Skip to the last window pkg in this delta. */
      while (pkg_skel->next)
        pkg_skel = pkg_skel->next;

      /* The offset is the first member of a window pkg. */
      offset_str = apr_pstrndup (trail->pool,
                                 pkg_skel->children->data,
                                 pkg_skel->children->len);

      /* Get the skel for the window sublist. */
      window_skel = pkg_skel->children->next;
      assert (window_skel != NULL);

      /* The window's reconstructed size is the second element. */
      size_str = apr_pstrndup (trail->pool,
                               window_skel->children->next->data,
                               window_skel->children->next->len);

      offset = atoi (offset_str);
      size = atoi (size_str);

      *size_p = (apr_size_t) (offset + size);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__rep_contents (svn_string_t *str,
                      svn_fs_t *fs,
                      const char *rep,
                      trail_t *trail)
{
  apr_size_t len;

  SVN_ERR (svn_fs__rep_contents_size (&(str->len), fs, rep, trail));
  str->data = apr_palloc (trail->pool, str->len);
  len = str->len;
  SVN_ERR (rep_read_range (fs, rep, (char *) str->data, 0, &len, trail));

  /* Paranoia. */
  if (len != str->len)
    return svn_error_createf
      (SVN_ERR_FS_CORRUPT, 0, NULL, trail->pool,
       "svn_fs__rep_read_contents: failure reading rep \"%s\"", rep);

  return SVN_NO_ERROR;
}


struct read_rep_args
{
  struct rep_read_baton *rb;   /* The data source.             */
  char *buf;                   /* Where to put what we read.   */
  apr_size_t *len;             /* How much to read / was read. */
};


/* BATON is of type `read_rep_args':

   Read into BATON->rb->buf the *(BATON->len) bytes starting at
   BATON->rb->offset from the data represented at BATON->rb->rep_key
   in BATON->rb->fs, as part of TRAIL.

   Afterwards, *(BATON->len) is the number of bytes actually read, and
   BATON->rb->offset is incremented by that amount.
   
   If BATON->rb->rep_key is null, this is assumed to mean the file's
   contents have no representation, i.e., the file has no contents.
   In that case, if BATON->rb->offset > 0, return the error
   SVN_ERR_FS_FILE_CONTENTS_CHANGED, else just set *(BATON->len) to
   zero and return.  */
static svn_error_t *
txn_body_read_rep (void *baton, trail_t *trail)
{
  struct read_rep_args *args = baton;

  if (args->rb->rep_key)
    {
      SVN_ERR (rep_read_range (args->rb->fs,
                               args->rb->rep_key,
                               args->buf,
                               args->rb->offset,
                               args->len,
                               trail));

      args->rb->offset += *(args->len);
    }
  else if (args->rb->offset > 0)
    {
      return
        svn_error_create
        (SVN_ERR_FS_REP_CHANGED, 0, NULL, trail->pool,
         "txn_body_read_rep: null rep, but offset past zero already");
    }
  else
    *(args->len) = 0;

  return SVN_NO_ERROR;
}


static svn_error_t *
rep_read_contents (void *baton, char *buf, apr_size_t *len)
{
  struct rep_read_baton *rb = baton;
  struct read_rep_args args;

  args.rb = rb;
  args.buf = buf;
  args.len = len;

  /* If we got a trail, use it; else make one. */
  if (rb->trail)
    SVN_ERR (txn_body_read_rep (&args, rb->trail));
  else
    SVN_ERR (svn_fs__retry_txn (rb->fs,
                                txn_body_read_rep,
                                &args,
                                rb->pool));
  
  return SVN_NO_ERROR;
}


/** Writing. **/


struct rep_write_baton
{
  /* The FS in which we're writing. */
  svn_fs_t *fs;

  /* The representation skel whose contents we want to write. */
  const char *rep_key;
  
  /* If present, do the write as part of this trail, and use trail's
     pool.  Otherwise, see `pool' below.  */ 
  trail_t *trail;

  /* Used for temporary allocations, iff `trail' (above) is null.  */
  apr_pool_t *pool;

};


static struct rep_write_baton *
rep_write_get_baton (svn_fs_t *fs,
                     const char *rep_key,
                     trail_t *trail,
                     apr_pool_t *pool)
{
  struct rep_write_baton *b;

  b = apr_pcalloc (pool, sizeof (*b));
  b->fs = fs;
  b->trail = trail;
  b->pool = pool;
  b->rep_key = rep_key;

  return b;
}



/* Write LEN bytes from BUF into the string represented via REP_KEY
   in FS, starting at OFFSET in that string, as part of TRAIL.

   If the representation is not mutable, return the error
   SVN_FS_REP_NOT_MUTABLE. */
static svn_error_t *
rep_write (svn_fs_t *fs,
           const char *rep_key,
           const char *buf,
           apr_size_t len,
           trail_t *trail)
{
  skel_t *rep;
        
  SVN_ERR (svn_fs__read_rep (&rep, fs, rep_key, trail));

  if (! rep_is_mutable (rep))
    svn_error_createf
      (SVN_ERR_FS_REP_CHANGED, 0, NULL, trail->pool,
       "rep_write: rep \"%s\" is not mutable", rep_key);

  if (rep_is_fulltext (rep))
    {
      const char *str_key;
      SVN_ERR (fulltext_string_key (&str_key, rep, trail->pool));
      SVN_ERR (svn_fs__string_append (fs, &str_key, len, buf, trail));
    }
  else
    {
      /* There should never be a case when we have a mutable
         non-fulltext rep.  The only code that creates mutable reps is
         in this file, and it creates them fulltext. */
      return svn_error_createf
        (SVN_ERR_FS_CORRUPT, 0, NULL, trail->pool,
         "rep_write: rep \"%s\" both mutable and non-fulltext", rep_key);
    }

  return SVN_NO_ERROR;
}


struct write_rep_args
{
  struct rep_write_baton *wb;   /* Destination.       */
  const char *buf;                 /* Data.              */
  apr_size_t len;                  /* How much to write. */
};


/* BATON is of type `write_rep_args':
   Append onto BATON->wb->rep_key's contents BATON->len bytes of
   data from BATON->wb->buf, in BATON->rb->fs, as part of TRAIL.  

   If the representation is not mutable, return the error
   SVN_FS_REP_NOT_MUTABLE.  */
static svn_error_t *
txn_body_write_rep (void *baton, trail_t *trail)
{
  struct write_rep_args *args = baton;

  SVN_ERR (rep_write (args->wb->fs,
                      args->wb->rep_key,
                      args->buf,
                      args->len,
                      trail));

  return SVN_NO_ERROR;
}


static svn_error_t *
rep_write_contents (void *baton, const char *buf, apr_size_t *len)
{
  struct rep_write_baton *wb = baton;
  struct write_rep_args args;

  /* We toss LEN's indirectness because if not all the bytes are
     written, it's an error, so we wouldn't be reporting anything back
     through *LEN anyway. */
  args.wb = wb;
  args.buf = buf;
  args.len = *len;

  /* If we got a trail, use it; else make one. */
  if (wb->trail)
    SVN_ERR (txn_body_write_rep (&args, wb->trail));
  else
    SVN_ERR (svn_fs__retry_txn (wb->fs,
                                txn_body_write_rep,
                                &args,
                                wb->pool));
  
  return SVN_NO_ERROR;
}


/** Public read and write stream constructors. **/

svn_stream_t *
svn_fs__rep_contents_read_stream (svn_fs_t *fs,
                                  const char *rep,
                                  apr_size_t offset,
                                  trail_t *trail,
                                  apr_pool_t *pool)
{
  struct rep_read_baton *rb
    = rep_read_get_baton (fs, rep, offset, trail, pool);

  svn_stream_t *rs = svn_stream_create (rb, pool);
  svn_stream_set_read (rs, rep_read_contents);

  return rs;
}

                                       
svn_stream_t *
svn_fs__rep_contents_write_stream (svn_fs_t *fs,
                                   const char *rep,
                                   trail_t *trail,
                                   apr_pool_t *pool)
{
  struct rep_write_baton *wb
    = rep_write_get_baton (fs, rep, trail, pool);

  svn_stream_t *ws = svn_stream_create (wb, pool);
  svn_stream_set_write (ws, rep_write_contents);

  return ws;
}


svn_error_t *
svn_fs__rep_contents_clear (svn_fs_t *fs,
                            const char *rep,
                            trail_t *trail)
{
  skel_t *rep_skel;
  const char *str_key;

  SVN_ERR (svn_fs__read_rep (&rep_skel, fs, rep, trail));

  /* Make sure it's mutable. */
  if (! rep_is_mutable (rep_skel))
    return svn_error_createf
      (SVN_ERR_FS_REP_NOT_MUTABLE, 0, NULL, trail->pool,
       "svn_fs__rep_contents_clear: rep \"%s\" is not mutable", rep);

  if (rep_is_fulltext (rep_skel))
    {
      SVN_ERR (fulltext_string_key (&str_key, rep_skel, trail->pool));

      /* If rep is already clear, just return success. */
      if ((str_key == NULL) || (str_key[0] == '\0'))
        return SVN_NO_ERROR;

      /* Else, clear it. */
      SVN_ERR (svn_fs__string_clear (fs, str_key, trail));
    }
  else /* delta */
    {
      /* For deltas, we replace the rep with a `fulltext' rep, then
         delete all the strings associated with the old rep. */
      apr_array_header_t *orig_keys;

      /* Get the list of strings associated with this rep. */
      SVN_ERR (delta_string_keys (&orig_keys, rep_skel, trail->pool));
      
      /* Transform our rep into a `fulltext' rep with an empty string
         behind it, and replace it in the filesystem. */
      str_key = NULL;
      SVN_ERR (svn_fs__string_append (fs, &str_key, 0, NULL, trail));
      rep_skel = make_fulltext_rep_skel (str_key, 1, trail->pool);
      SVN_ERR (svn_fs__write_rep (fs, rep, rep_skel, trail));

      /* Now delete those old strings. */
      SVN_ERR (delete_strings (orig_keys, fs, trail));
    }

  return SVN_NO_ERROR;
}



/*** Deltified storage. ***/

/* Baton for svn_write_fn_t write_string(). */
struct write_string_baton
{
  /* The fs where lives the string we're writing. */
  svn_fs_t *fs;

  /* The key of the string we're writing to.  Typically this is
     initialized to NULL, so svn_fs__string_append() can fill in a
     value. */
  const char *key;

  /* The trail we're writing in. */
  trail_t *trail;
};


/* Function of type `svn_write_fn_t', for writing to a string;
   BATON is `struct write_string_baton *'.

   On the first call, BATON->key is null.  A new string key in
   BATON->fs is chosen and stored in BATON->key; each call appends
   *LEN bytes from DATA onto the string.  *LEN is never changed; if
   the write fails to write all *LEN bytes, an error is returned.  */
static svn_error_t *
write_string (void *baton, const char *data, apr_size_t *len)
{
  struct write_string_baton *wb = baton;
  return svn_fs__string_append (wb->fs, &(wb->key), *len, data, wb->trail);
}


/* Baton for svn_write_fn_t write_string_set(). */
struct write_svndiff_strings_baton
{
  /* The fs where lives the string we're writing. */
  svn_fs_t *fs;

  /* The key of the string we're writing to.  Typically this is
     initialized to NULL, so svn_fs__string_append() can fill in a
     value. */
  const char *key;

  /* The amount of txdelta data written to the current
     string-in-progress. */
  apr_size_t size;

  /* The amount of svndiff header information we've written thus far
     to the strings table. */
  apr_size_t header_read;

  /* The trail we're writing in. */
  trail_t *trail;

};


/* Function of type `svn_write_fn_t', for writing to a collection of
   strings; BATON is `struct write_svndiff_strings_baton *'.

   On the first call, BATON->key is null.  A new string key in
   BATON->fs is chosen and stored in BATON->key; each call appends
   *LEN bytes from DATA onto the string.  *LEN is never changed; if
   the write fails to write all *LEN bytes, an error is returned.
   BATON->size is used to track the total amount of data written via
   this handler, and must be reset by the caller to 0 when appropriate.  */
static svn_error_t *
write_svndiff_strings (void *baton, const char *data, apr_size_t *len)
{
  struct write_svndiff_strings_baton *wb = baton;
  const char *buf = data;
  apr_size_t nheader = 0;

  /* If we haven't stripped all the header information from this
     stream yet, keep stripping.  If someone sends a first window
     through here that's shorter than 4 bytes long, this will probably
     cause a nuclear reactor meltdown somewhere in the American
     midwest.  */
  if (wb->header_read < 4)
    {
      nheader = 4 - wb->header_read;
      *len -= nheader;
      buf += nheader;
      wb->header_read += nheader;
    }

  /* Append to the current string we're writing (or create a new one
     if WB->key is NULL). */
  SVN_ERR (svn_fs__string_append (wb->fs, &(wb->key), *len, buf, wb->trail));

  /* Make sure we (still) have a key. */
  if (wb->key == NULL)
    return svn_error_create (SVN_ERR_FS_GENERAL, 0, NULL, wb->trail->pool,
                             "write_string_set: Failed to get new string key");

  /* Restore *LEN to the value it *would* have been were it not for
     header stripping. */
  *len += nheader;

  /* Increment our running total of bytes written to this string. */
  wb->size += *len;

  return SVN_NO_ERROR;
}


typedef struct window_write_t
{
  const char *key; /* string key for this window */
  apr_size_t svndiff_len; /* amount of svndiff data written to the string */
  apr_size_t text_off; /* offset of fulltext data represented by this window */
  apr_size_t text_len; /* amount of fulltext data represented by this window */

} window_write_t;


svn_error_t *
svn_fs__rep_deltify (svn_fs_t *fs,
                     const char *target,
                     const char *source,
                     trail_t *trail)
{
  svn_stream_t *source_stream; /* stream to read the source */
  svn_stream_t *target_stream; /* stream to read the target */
  svn_txdelta_stream_t *txdelta_stream; /* stream to read delta windows  */

  /* window-y things, and an array to track them */
  window_write_t *ww;
  apr_array_header_t *windows;

  /* stream to write new (deltified) target data and its baton */
  svn_stream_t *new_target_stream;
  struct write_svndiff_strings_baton new_target_baton;
  
  /* window handler/baton for writing to above stream */
  svn_txdelta_window_handler_t new_target_handler;
  void *new_target_handler_baton;
  
  /* yes, we do windows */
  svn_txdelta_window_t *window;

  /* The current offset into the fulltext that our window is about to
     write.  This doubles, after all windows are written, as the
     total size of the svndiff data for the deltification process. */
  apr_size_t tview_off = 0;

  /* The total amount of diff data written while deltifying. */
  apr_size_t diffsize = 0;

  /* TARGET's original string keys */
  apr_array_header_t *orig_str_keys;
  
  /* MD5 digest */
  const unsigned char *digest;

  /* pool for holding the windows */
  apr_pool_t *wpool;

  /* Paranoia: never allow a rep to be deltified against itself,
     because then there would be no fulltext reachable in the delta
     chain, and badness would ensue.  */
  if (strcmp (target, source) == 0)
    return svn_error_createf
      (SVN_ERR_FS_CORRUPT, 0, NULL, trail->pool,
       "svn_fs__rep_deltify: attempt to deltify \"%s\" against itself",
       target);

  /* To favor time over space, we don't currently deltify files that
     are larger than the svndiff window size.  This might seem
     counterintuitive, but most files are smaller than a window
     anyway, and until we write the delta combiner or something
     approaching it, the cost of retrieval for large files becomes
     simply prohibitive after about 10 or so revisions.  See issue
     #531 for more details. */
  {
    apr_size_t size;
    SVN_ERR (svn_fs__rep_contents_size (&size, fs, target, trail));
    if (size > SVN_STREAM_CHUNK_SIZE)
      return SVN_NO_ERROR;
  }

  /* Set up a handler for the svndiff data, which will write each
     window to its own string in the `strings' table. */
  new_target_baton.fs = fs;
  new_target_baton.trail = trail;
  new_target_baton.header_read = 0;
  new_target_stream = svn_stream_create (&new_target_baton, trail->pool);
  svn_stream_set_write (new_target_stream, write_svndiff_strings);

  /* Get streams to our source and target text data. */
  source_stream = svn_fs__rep_contents_read_stream (fs, source, 0,
                                                    trail, trail->pool);
  target_stream = svn_fs__rep_contents_read_stream (fs, target, 0,
                                                    trail, trail->pool);

  /* Setup a stream to convert the textdelta data into svndiff windows. */
  svn_txdelta (&txdelta_stream, source_stream, target_stream, trail->pool);
  svn_txdelta_to_svndiff (new_target_stream, trail->pool,
                          &new_target_handler, &new_target_handler_baton);

  /* subpool for the windows */
  wpool = svn_pool_create (trail->pool);

  /* Now, loop, manufacturing and dispatching windows of svndiff data. */
  windows = apr_array_make (trail->pool, 1, sizeof (ww));
  do
    {
      /* Reset some baton variables. */
      new_target_baton.size = 0;
      new_target_baton.key = NULL;

      /* Fetch the next window of txdelta data. */
      SVN_ERR (svn_txdelta_next_window (&window, txdelta_stream, wpool));

      /* Send off this package to be written as svndiff data. */
      SVN_ERR (new_target_handler (window, new_target_handler_baton));
      if (window)
        {
          /* Add a new window description to our array. */
          ww = apr_pcalloc (trail->pool, sizeof (*ww));
          ww->key = new_target_baton.key;
          ww->svndiff_len = new_target_baton.size;
          ww->text_off = tview_off;
          ww->text_len = window->tview_len;
          (*((window_write_t **)(apr_array_push (windows)))) = ww;

          /* Update our recordkeeping variables. */
          tview_off += window->tview_len;
          diffsize += ww->svndiff_len;
           
          /* Free the window. */
          svn_pool_clear (wpool);
        }

    } while (window);

  svn_pool_destroy (wpool);

  /* Having processed all the windows, we can query the MD5 digest
     from the stream.  */
  digest = svn_txdelta_md5_digest (txdelta_stream);
  if (! digest)
    return svn_error_createf
      (SVN_ERR_DELTA_MD5_CHECKSUM_ABSENT, 0, NULL, trail->pool,
       "svn_fs__rep_deltify: failed to calculate MD5 digest for %s",
       source);

  /* Get the size of the target's original string data.  Note that we
     don't use svn_fs__rep_contents_size() for this; that function
     always returns the fulltext size, whereas we need to know the
     actual amount of storage used by this representation.  Check the
     size of the new string.  If it is larger than the old one, this
     whole deltafication might not be such a bright idea.  While we're
     at it, we might as well figure out all the strings current used
     by REP so we can potentially delete them later. */
  {
    skel_t *old_rep;
    apr_size_t old_size = 0;
    const char *str_key;

    SVN_ERR (svn_fs__read_rep (&old_rep, fs, target, trail));
    if (rep_is_fulltext (old_rep))
      {
        SVN_ERR (fulltext_string_key (&str_key, old_rep, trail->pool));
        SVN_ERR (svn_fs__string_size (&old_size, fs, str_key, trail));
        orig_str_keys = apr_array_make (trail->pool, 1, sizeof (str_key));
        (*((const char **)(apr_array_push (orig_str_keys)))) = str_key;
      }
    else
      {
        int i;
        apr_size_t my_size;
        
        SVN_ERR (delta_string_keys (&orig_str_keys, old_rep, trail->pool));
        for (i = 0; i < orig_str_keys->nelts; i++)
          {
            str_key = ((const char **)(orig_str_keys->elts))[i];
            SVN_ERR (svn_fs__string_size (&my_size, fs, str_key, trail));
            old_size += my_size;
          }
      }

    /* If the new data is NOT an space optimization, destroy the
       string(s) we created, and get outta here. */
    if (diffsize >= old_size)
      {
        int i;
        for (i = 0; i < windows->nelts; i++)
          {
            ww = ((window_write_t **)(windows->elts))[i];
            SVN_ERR (svn_fs__string_delete (fs, ww->key, trail));
          }
        return SVN_NO_ERROR;
      }
  }

  /* Hook the new strings we wrote into the rest of the filesystem by
     building a new representation skel to replace our old one. */
  {
    int i;
    skel_t *rep      = svn_fs__make_empty_list (trail->pool);
    skel_t *header   = svn_fs__make_empty_list (trail->pool);

    /* Loop backwards through the windows we wrote, creating and
       prepending skels to our rep. */
    for (i = windows->nelts; i > 0; i--)
      {
        const char *size_str;
        const char *offset_str;
        skel_t *win      = svn_fs__make_empty_list (trail->pool);
        skel_t *winhdr   = svn_fs__make_empty_list (trail->pool);
        skel_t *diff     = svn_fs__make_empty_list (trail->pool);
        skel_t *checksum = svn_fs__make_empty_list (trail->pool);

        ww = ((window_write_t **)(windows->elts))[i-1];

        offset_str = apr_psprintf (trail->pool, "%" APR_SIZE_T_FMT,
                                   ww->text_off);
        size_str = apr_psprintf (trail->pool, "%" APR_SIZE_T_FMT,
                                 ww->text_len);

        /* The diff. */
        svn_fs__prepend (svn_fs__str_atom (ww->key, trail->pool), diff);
        svn_fs__prepend (svn_fs__str_atom ("svndiff", trail->pool), diff);
        
        /* The checksum. */
        svn_fs__prepend (svn_fs__mem_atom (digest, 
                                           MD5_DIGESTSIZE, 
                                           trail->pool), checksum);
        svn_fs__prepend (svn_fs__str_atom ("md5", trail->pool), checksum);

        /* The window. */
        svn_fs__prepend (svn_fs__str_atom (source, trail->pool), win);
        svn_fs__prepend (checksum, win);
        svn_fs__prepend (svn_fs__str_atom (size_str, trail->pool), win);
        svn_fs__prepend (diff, win);

        /* The window header. */
        svn_fs__prepend (win, winhdr);
        svn_fs__prepend (svn_fs__str_atom (offset_str, trail->pool), winhdr);

        /* Add this window item to the rep. */
        svn_fs__prepend (winhdr, rep);
      }

    /* Don't forget to prepend the header! */
    svn_fs__prepend (svn_fs__str_atom ("delta", trail->pool), header);
    svn_fs__prepend (header, rep);

    /* Write out the new representation. */
    SVN_ERR (svn_fs__write_rep (fs, target, rep, trail));
    SVN_ERR (delete_strings (orig_str_keys, fs, trail));
  }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__rep_undeltify (svn_fs_t *fs,
                       const char *rep,
                       trail_t *trail)
{
  /* ### todo:  Make this thing `delta'-aware! */
  svn_stream_t *source_stream; /* stream to read the source */
  svn_stream_t *target_stream; /* stream to write the fulltext */
  struct write_string_baton target_baton;
  apr_array_header_t *orig_keys;
  skel_t *rep_skel;
  unsigned char buf[65536];
  apr_size_t len;

  /* Read the rep skel. */
  SVN_ERR (svn_fs__read_rep (&rep_skel, fs, rep, trail));

  /* If REP is a fulltext rep, there's nothing to do. */
  if (rep_is_fulltext (rep_skel))
    return SVN_NO_ERROR;

  /* Get the original string keys from REP (so we can delete them after
     we write our new skel out. */
  SVN_ERR (delta_string_keys (&orig_keys, rep_skel, trail->pool));
  
  /* Set up a string to receive the svndiff data. */
  target_baton.fs = fs;
  target_baton.trail = trail;
  target_baton.key = NULL;
  target_stream = svn_stream_create (&target_baton, trail->pool);
  svn_stream_set_write (target_stream, write_string);

  /* Set up the source stream. */
  source_stream = svn_fs__rep_contents_read_stream (fs, rep, 0,
                                                    trail, trail->pool);
  do
    {
      apr_size_t len_read;

      len = sizeof (buf);
      SVN_ERR (svn_stream_read (source_stream, buf, &len));
      len_read = len;
      SVN_ERR (svn_stream_write (target_stream, buf, &len));
      if (len_read != len)
        return svn_error_createf 
          (SVN_ERR_FS_GENERAL, 0, NULL, trail->pool,
           "svn_fs__rep_undeltify: Error writing fulltext contents");
    }
  while (len);

  /* Now `target_baton.key' has the key of the new string.  We
     should hook it into the representation.  So we make a new rep,
     write it out... */
  rep_skel = make_fulltext_rep_skel (target_baton.key, 0, trail->pool);
  SVN_ERR (svn_fs__write_rep (fs, rep, rep_skel, trail));

  /* ...then we delete our original strings. */
  SVN_ERR (delete_strings (orig_keys, fs, trail));

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */

