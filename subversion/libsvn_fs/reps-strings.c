/* reps-strings.c : intepreting representations w.r.t. strings
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#include <string.h>
#include "db.h"
#include "svn_fs.h"
#include "fs.h"
#include "err.h"
#include "dbt.h"
#include "trail.h"
#include "reps-table.h"
#include "strings-table.h"
#include "reps-strings.h"



/* Helper function.  Is representation a `fulltext' type?  */
static int
rep_is_fulltext (skel_t *rep)
{
  return svn_fs__matches_atom (rep->children->children, "fulltext");
}


const char *
svn_fs__string_key_from_rep (skel_t *rep, apr_pool_t *pool)
{
  if (rep_is_fulltext (rep))
    return apr_pstrndup (pool,
                         rep->children->next->data,
                         rep->children->next->len);
  else
    {
      skel_t *diff = rep->children->next->next;

      if (strncmp ("svndiff", diff->children->data, diff->children->len) != 0)
        abort ();  /* unknown delta format rep */

      return apr_pstrndup (pool,
                           diff->children->next->data,
                           diff->children->next->len);
    }

  return NULL;
}


svn_error_t *
svn_fs__rep_contents (svn_string_t *str,
                      svn_fs_t *fs,
                      const char *rep_key,
                      trail_t *trail)
{
  const char *strkey;
  char *data;
  skel_t *rep;

  SVN_ERR (svn_fs__read_rep (&rep, fs, rep_key, trail));
  strkey = svn_fs__string_key_from_rep (rep, trail->pool);
  SVN_ERR (svn_fs__string_size (&(str->len), fs, strkey, trail));
  data = apr_palloc (trail->pool, str->len);
  SVN_ERR (svn_fs__string_read (fs, strkey, data, 0, &(str->len), trail));
  str->data = data;
  return SVN_NO_ERROR;
}


int
svn_fs__rep_is_mutable (skel_t *rep)
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
  if (! svn_fs__rep_is_mutable (rep))
    svn_fs__append (svn_fs__str_atom ("mutable", pool), rep->children);
    
  return;
}


svn_error_t *
svn_fs__get_mutable_rep (const char **new_key,
                         const char *key,
                         svn_fs_t *fs, 
                         trail_t *trail)
{
  skel_t *rep;

  if (key && (key[0] != '\0'))
    {
      SVN_ERR (svn_fs__read_rep (&rep, fs, key, trail));
  
      /* If REP is not mutable, we have to make a mutable copy.  It is
         a deep copy -- the underlying string is copied too, and the
         new rep refers to the new string.  */
      if (! svn_fs__rep_is_mutable (rep))
        {
          if (rep_is_fulltext (rep))
            {
              const char *string_key, *new_string_key;
              
              /* Step 1:  Copy the string to which the rep refers. */
              string_key = svn_fs__string_key_from_rep (rep, trail->pool);
              SVN_ERR (svn_fs__string_copy (fs, &new_string_key,
                                            string_key, trail));
              
              /* Step 2:  Make this rep mutable. */
              rep_set_mutable_flag (rep, trail->pool);
              
              /* Step 3:  Change the string key to which this rep points. */
              rep->children->next->data = new_string_key;
              rep->children->next->len = strlen (new_string_key);
              
              /* Step 4: Write the mutable version of this rep to the
                 database, returning the newly created key to the
                 caller. */
              SVN_ERR (svn_fs__write_new_rep (new_key, fs, rep, trail));
            }
          else
            abort (); /* Huh?  We only know about fulltext right now. */
        }
      else
        *new_key = key;
    }
  else    /* no key, so make a new, empty, mutable, fulltext rep */
    {
      const char *new_string_key = NULL;
      skel_t *header = svn_fs__make_empty_list (trail->pool);
      rep = svn_fs__make_empty_list (trail->pool);

      svn_fs__prepend (svn_fs__str_atom ("mutable", trail->pool), header);
      svn_fs__prepend (svn_fs__str_atom ("fulltext", trail->pool), header);

      /* Create a new, empty string. */
      SVN_ERR (svn_fs__string_append (fs, &new_string_key, 0, NULL, trail));
      
      svn_fs__prepend (svn_fs__str_atom (new_string_key, trail->pool), rep);
      svn_fs__prepend (header, rep);
      
      SVN_ERR (svn_fs__write_new_rep (new_key, fs, rep, trail));
    }
      
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__make_rep_immutable (svn_fs_t *fs,
                            const char *key,
                            trail_t *trail)
{
  skel_t *rep;
  skel_t *header, *flag, *prev;

  SVN_ERR (svn_fs__read_rep (&rep, fs, key, trail));
  header = rep->children;

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
          
          SVN_ERR (svn_fs__write_rep (fs, key, rep, trail));
          break;
        }
    }
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__delete_rep_if_mutable (svn_fs_t *fs,
                               const char *key,
                               trail_t *trail)
{
  skel_t *rep;

  SVN_ERR (svn_fs__read_rep (&rep, fs, key, trail));
  if (svn_fs__rep_is_mutable (rep))
    {
      const char *string_key;
      string_key = svn_fs__string_key_from_rep (rep, trail->pool);
      SVN_ERR (svn_fs__string_delete (fs, string_key, trail));
      SVN_ERR (svn_fs__delete_rep (fs, key, trail));
    }

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
        
  SVN_ERR (svn_fs__read_rep (&rep, fs, rep_key, trail));

  if (rep_is_fulltext (rep))
    {
      const char *string_key = apr_pstrndup (trail->pool,
                                             rep->children->next->data,
                                             rep->children->next->len);
      SVN_ERR (svn_fs__string_read (fs, string_key, buf, offset, len, trail));
    }
  else
    abort ();  /* We don't do undeltification yet. */

  return SVN_NO_ERROR;
}



/*** Retrieving data. ***/

svn_error_t *
svn_fs__rep_contents_size (apr_size_t *size,
                           svn_fs_t *fs,
                           const char *rep_key,
                           trail_t *trail)
{
  skel_t *rep;
  const char *str_key;

  SVN_ERR (svn_fs__read_rep (&rep, fs, rep_key, trail));
  str_key = svn_fs__string_key_from_rep (rep, trail->pool);
  SVN_ERR (svn_fs__string_size (size, fs, str_key, trail));

  return SVN_NO_ERROR;
}


struct read_rep_args
{
  struct rep_read_baton *rb;   /* The data source.             */
  char *buf;                      /* Where to put what we read.   */
  apr_size_t *len;                /* How much to read / was read. */
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

  if (! svn_fs__rep_is_mutable (rep))
    svn_error_createf
      (SVN_ERR_FS_REP_CHANGED, 0, NULL, trail->pool,
       "rep_write: rep \"%s\" is not mutable", rep_key);

  if (rep_is_fulltext (rep))
    {
      const char *string_key = apr_pstrndup (trail->pool,
                                             rep->children->next->data,
                                             rep->children->next->len);

      SVN_ERR (svn_fs__string_append (fs, &string_key, len, buf, trail));
    }
  else
    abort ();  /* We don't do deltification yet. */

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
                                  const char *rep_key,
                                  apr_size_t offset,
                                  trail_t *trail,
                                  apr_pool_t *pool)
{
  struct rep_read_baton *rb
    = rep_read_get_baton (fs, rep_key, offset, trail, pool);

  svn_stream_t *rs = svn_stream_create (rb, pool);
  svn_stream_set_read (rs, rep_read_contents);

  return rs;
}

                                       
svn_stream_t *
svn_fs__rep_contents_write_stream (svn_fs_t *fs,
                                   const char *rep_key,
                                   trail_t *trail,
                                   apr_pool_t *pool)
{
  struct rep_write_baton *wb
    = rep_write_get_baton (fs, rep_key, trail, pool);

  svn_stream_t *ws = svn_stream_create (wb, pool);
  svn_stream_set_write (ws, rep_write_contents);

  return ws;
}


svn_error_t *
svn_fs__rep_contents_clear (svn_fs_t *fs,
                            const char *rep_key,
                            trail_t *trail)
{
  skel_t *rep;
  const char *str_key;

  SVN_ERR (svn_fs__read_rep (&rep, fs, rep_key, trail));

  /* Make sure it's mutable. */
  if (! svn_fs__rep_is_mutable (rep))
    return svn_error_createf
      (SVN_ERR_FS_REP_NOT_MUTABLE, 0, NULL, trail->pool,
       "svn_fs__rep_contents_clear: rep \"%s\" is not mutable", rep_key);

  str_key = svn_fs__string_key_from_rep (rep, trail->pool);

  /* If rep is already clear, just return success. */
  if ((str_key == NULL) || (str_key[0] == '\0'))
    return SVN_NO_ERROR;

  /* Else, clear it. */

  if (rep_is_fulltext (rep))
    {
      SVN_ERR (svn_fs__string_clear (fs, str_key, trail));
    }
  else  /* delta */
    {
      /* I can't actually imagine that this case would ever be
         reached.  When would you clear a deltified representation,
         since the fact that it's deltified implies that the node
         referring to it has been stabilized?  But that logic is
         outside the scope of this function.  We shouldn't refuse to
         clear a deltified rep just because we don't understand why
         someone wants to. */

      /* ### todo: we could convert the rep to fulltext, but instead
         we keep it in delta form, to avoid losing the base rep
         information.  We just change the svndiff data to a
         bone-simple delta that converts any base text to an empty
         target string.  */

      /* The universal null delta is the four bytes 'S' 'V' 'N' '\0'. */
      const char *null_delta = "SVN";

      SVN_ERR (svn_fs__string_clear (fs, str_key, trail));
      SVN_ERR (svn_fs__string_append (fs, &str_key, 4, null_delta, trail));
    }

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */

