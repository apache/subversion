/* load.c --- parsing a 'dumpfile'-formatted stream.
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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


#include "svn_pools.h"
#include "svn_error.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_string.h"
#include "svn_hash.h"
#include "svn_path.h"

#include <apr_lib.h>



/*----------------------------------------------------------------------*/

/** Batons used herein **/

struct parse_baton
{
  svn_repos_t *repos;
  svn_fs_t *fs;

  svn_boolean_t use_history;
  svn_stream_t *outstream;
};

struct revision_baton
{
  svn_revnum_t rev;

  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;

  const svn_string_t *datestamp;

  apr_int32_t rev_offset;

  struct parse_baton *pb;
  apr_pool_t *pool;
};

struct node_baton
{
  const char *path;
  svn_node_kind_t kind;
  enum svn_node_action action;

  svn_revnum_t copyfrom_rev;
  const char *copyfrom_path;

  struct revision_baton *rb;
  apr_pool_t *pool;
};



/*----------------------------------------------------------------------*/

/** The parser and related helper funcs **/


/* Allocate a new hash *HEADERS in POOL, and read a series of
   RFC822-style headers from STREAM.  Duplicate each header's name and
   value into POOL and store in hash as a const char * ==> const char *.

   The headers are assumed to be terminated by a single blank line,
   which will be permanently sucked from the stream and tossed.

   If the caller has already read in the first header line, it should
   be passed in as FIRST_HEADER.  If not, pass NULL instead.
 */
static svn_error_t *
read_header_block (svn_stream_t *stream,
                   svn_stringbuf_t *first_header,
                   apr_hash_t **headers,
                   apr_pool_t *pool)
{
  *headers = apr_hash_make (pool);  

  while (1)
    {
      svn_stringbuf_t *header_str;
      const char *name, *value; 
      apr_size_t i = 0;

      if (first_header != NULL)
        {
          header_str = first_header;
          first_header = NULL;  /* so we never visit this block again. */
        }

      else
        /* Read the next line into a stringbuf. */
        SVN_ERR (svn_stream_readline (stream, &header_str, pool));
      
      if ((header_str == NULL) || (svn_stringbuf_isempty (header_str)))
        break;    /* end of header block */

      /* Find the next colon in the stringbuf. */
      while (header_str->data[i] != ':')
        {
          if (header_str->data[i] == '\0')
            return svn_error_create (SVN_ERR_STREAM_MALFORMED_DATA, NULL,
                                     "Found malformed header block "
                                     "in dumpfile stream.");
          i++;
        }
      /* Create a 'name' string and point to it. */
      header_str->data[i] = '\0';
      name = header_str->data;

      /* Skip over the NULL byte and the space following it.  */
      i += 2;
      if (i > header_str->len)
        return svn_error_create (SVN_ERR_STREAM_MALFORMED_DATA, NULL,
                                 "Found malformed header block "
                                 "in dumpfile stream.");

      /* Point to the 'value' string. */
      value = header_str->data + i;
      
      /* Store name/value in hash. */
      apr_hash_set (*headers, name, APR_HASH_KEY_STRING, value);
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
stream_ran_dry (void)
{
  return svn_error_create (SVN_ERR_INCOMPLETE_DATA, NULL,
                           "Premature end of content data in dumpstream.");
}

static svn_error_t *
stream_malformed (void)
{
  return svn_error_create (SVN_ERR_STREAM_MALFORMED_DATA, NULL,
                           "Dumpstream data appears to be malformed.");
}

/* Read CONTENT_LENGTH bytes from STREAM, parsing the bytes as an
   encoded Subversion properties hash, and making multiple calls to
   PARSE_FNS->set_*_property on RECORD_BATON (depending on the value
   of IS_NODE.)

   Use POOL for all allocations.  */
static svn_error_t *
parse_property_block (svn_stream_t *stream,
                      apr_size_t content_length,
                      const svn_repos_parser_fns_t *parse_fns,
                      void *record_baton,
                      svn_boolean_t is_node,
                      apr_pool_t *pool)
{
  svn_stringbuf_t *strbuf;

  while (content_length)
    {
      char *buf;  /* a pointer into the stringbuf's data */

      /* Read a key length line.  (Actually, it might be PROPS_END). */
      SVN_ERR (svn_stream_readline (stream, &strbuf, pool));

      if (strbuf == NULL)
        {
          /* We could just use stream_ran_dry() or stream_malformed(),
             but better to give a non-generic property block error. */ 
          return svn_error_create
            (SVN_ERR_STREAM_MALFORMED_DATA, NULL,
             "incomplete or unterminated property block");
        }

      content_length -= (strbuf->len + 1); /* +1 because we read a \n too. */
      buf = strbuf->data;

      if (! strcmp (buf, "PROPS-END"))
        break; /* no more properties. */

      else if ((buf[0] == 'K') && (buf[1] == ' '))
        {
          apr_size_t numread;
          char *keybuf;
          char c;
          
          /* Get the length of the key */
          apr_size_t keylen = (apr_size_t) atoi (buf + 2);

          /* Now read that much into a buffer, + 1 byte for null terminator */
          keybuf = apr_pcalloc (pool, keylen + 1);
          numread = keylen;
          SVN_ERR (svn_stream_read (stream, keybuf, &numread));
          content_length -= numread;
          if (numread != keylen)
            return stream_ran_dry ();
          keybuf[keylen] = '\0';

          /* Suck up extra newline after key data */
          numread = 1;
          SVN_ERR (svn_stream_read (stream, &c, &numread));
          content_length -= numread;
          if (numread != 1)
            return stream_ran_dry ();
          if (c != '\n') 
            return stream_malformed ();

          /* Read a val length line */
          SVN_ERR (svn_stream_readline (stream, &strbuf, pool));
          content_length -= (strbuf->len + 1); /* +1 because we read \n too */
          buf = strbuf->data;

          if ((buf[0] == 'V') && (buf[1] == ' '))
            {
              svn_string_t propstring;

              /* Get the length of the value */
              apr_size_t vallen = atoi (buf + 2);

              /* Again, 1 extra byte for the null termination. */
              char *valbuf = apr_palloc (pool, vallen + 1);
              numread = vallen;
              SVN_ERR (svn_stream_read (stream, valbuf, &numread));
              content_length -= numread;
              if (numread != vallen)
                return stream_ran_dry ();
              ((char *) valbuf)[vallen] = '\0';

              /* Suck up extra newline after val data */
              numread = 1;
              SVN_ERR (svn_stream_read (stream, &c, &numread));
              content_length -= numread;
              if (numread != 1)
                return stream_ran_dry ();
              if (c != '\n') 
                return stream_malformed ();

              /* Create final value string */
              propstring.data = valbuf;
              propstring.len = vallen;

              /* Now, send the property pair to the vtable! */
              if (is_node)
                SVN_ERR (parse_fns->set_node_property (record_baton,
                                                       keybuf,
                                                       &propstring));
              else
                SVN_ERR (parse_fns->set_revision_property (record_baton,
                                                           keybuf,
                                                           &propstring));
            }
          else
            return stream_malformed (); /* didn't find expected 'V' line */
        }
      else
        return stream_malformed (); /* didn't find expected 'K' line */
      
    } /* while (1) */

  return SVN_NO_ERROR;
}                  


/* Read CONTENT_LENGTH bytes from STREAM, and use
   PARSE_FNS->set_fulltext to push those bytes as replace fulltext for
   a node.  Use BUFFER/BUFLEN to push the fulltext in "chunks".

   Use POOL for all allocations.  */
static svn_error_t *
parse_text_block (svn_stream_t *stream,
                  apr_size_t content_length,
                  const svn_repos_parser_fns_t *parse_fns,
                  void *record_baton,
                  char *buffer,
                  apr_size_t buflen,
                  apr_pool_t *pool)
{
  svn_stream_t *text_stream = NULL;
  apr_size_t num_to_read, rlen, wlen;
  
  /* Get a stream to which we can push the data. */
  SVN_ERR (parse_fns->set_fulltext (&text_stream, record_baton));

  /* If there are no contents to read, just write an empty buffer
     through our callback. */
  if (content_length == 0)
    {
      wlen = 0;
      if (text_stream)
        SVN_ERR (svn_stream_write (text_stream, "", &wlen));
    }

  /* Regardless of whether or not we have a sink for our data, we
     need to read it. */
  while (content_length)
    {
      if (content_length >= buflen)
        rlen = buflen;
      else
        rlen = content_length;
      
      num_to_read = rlen;
      SVN_ERR (svn_stream_read (stream, buffer, &rlen));
      content_length -= rlen;
      if (rlen != num_to_read)
        return stream_ran_dry ();
      
      if (text_stream)
        {
          /* write however many bytes you read. */
          wlen = rlen;
          SVN_ERR (svn_stream_write (text_stream, buffer, &wlen));
          if (wlen != rlen)
            {
              /* Uh oh, didn't write as many bytes as we read. */
              return svn_error_create (SVN_ERR_STREAM_UNEXPECTED_EOF, NULL,
                                       "Error pushing textual contents.");
            }
        }
    }
  
  /* If we opened a stream, we must close it. */
  if (text_stream)
    SVN_ERR (svn_stream_close (text_stream));

  return SVN_NO_ERROR;
}



static svn_error_t *
validate_format_version (const char *versionstring)
{
  /* ### parse string and verify that we support the dumpfile format
         version number. */
  
  return SVN_NO_ERROR;
}



/* The Main Parser Logic */
svn_error_t *
svn_repos_parse_dumpstream (svn_stream_t *stream,
                            const svn_repos_parser_fns_t *parse_fns,
                            void *parse_baton,
                            apr_pool_t *pool)
{
  svn_stringbuf_t *linebuf;
  void *rev_baton = NULL;
  char *buffer = apr_palloc (pool, SVN_STREAM_CHUNK_SIZE);
  apr_size_t buflen = SVN_STREAM_CHUNK_SIZE;
  apr_pool_t *linepool = svn_pool_create (pool);
  apr_pool_t *revpool = svn_pool_create (pool);
  apr_pool_t *nodepool = svn_pool_create (pool);

  SVN_ERR (svn_stream_readline (stream, &linebuf, linepool));
  if (linebuf == NULL)
    return stream_ran_dry ();
    
  /* The first two lines of the stream are the dumpfile-format version
     number, and a blank line. */
  SVN_ERR (validate_format_version (linebuf->data));

  /* A dumpfile "record" is defined to be a header-block of
     rfc822-style headers, possibly followed by a content-block.

       - A header-block is always terminated by a single blank line (\n\n)

       - We know whether the record has a content-block by looking for
         a 'Content-length:' header.  The content-block will always be
         of a specific length, plus an extra newline.

     Once a record is fully sucked from the stream, an indeterminate
     number of blank lines (or lines that begin with whitespace) may
     follow before the next record (or the end of the stream.)
  */
  
  while (1)
    {
      apr_hash_t *headers;
      void *node_baton;
      const char *valstr;
      svn_boolean_t found_node = FALSE;

      /* Clear our per-line pool. */
      svn_pool_clear (linepool);

      /* Keep reading blank lines until we discover a new record, or until
         the stream runs out. */
      SVN_ERR (svn_stream_readline (stream, &linebuf, linepool));
      
      if (linebuf == NULL)
        break;   /* end of stream, go home. */

      if ((linebuf->len == 0) || (apr_isspace (linebuf->data[0])))
        continue; /* empty line ... loop */

      /*** Found the beginning of a new record. ***/ 

      /* The last line we read better be a header of some sort.
         Read the whole header-block into a hash. */
      SVN_ERR (read_header_block (stream, linebuf, &headers, linepool));

      /* Create some kind of new record object. */

      /* Is this a revision record? */
      if (apr_hash_get (headers, SVN_REPOS_DUMPFILE_REVISION_NUMBER,
                        APR_HASH_KEY_STRING))
        {
          /* If we already have a rev_baton open, we need to close it
             and clear the per-revision subpool. */
          if (rev_baton != NULL)
            {
              SVN_ERR (parse_fns->close_revision (rev_baton));
              svn_pool_clear (revpool);
            }

          SVN_ERR (parse_fns->new_revision_record (&rev_baton,
                                                   headers, parse_baton,
                                                   revpool));
        }

      /* Or is this, perhaps, a node record? */
      else if (apr_hash_get (headers, SVN_REPOS_DUMPFILE_NODE_PATH,
                             APR_HASH_KEY_STRING))
        {
          SVN_ERR (parse_fns->new_node_record (&node_baton,
                                               headers,
                                               rev_baton,
                                               nodepool));
          found_node = TRUE;
        }

      /* Or is this bogosity?! */
      else
        {
          /* What the heck is this record?!? */
          return svn_error_create (SVN_ERR_STREAM_MALFORMED_DATA, NULL,
                                   "Unrecognized record type in stream.");
        }
      
      /* Is there a props content-block to parse? */
      if ((valstr = apr_hash_get (headers,
                                  SVN_REPOS_DUMPFILE_PROP_CONTENT_LENGTH,
                                  APR_HASH_KEY_STRING)))
        {
          SVN_ERR (parse_property_block (stream, 
                                         (apr_size_t) atoi (valstr),
                                         parse_fns,
                                         found_node ? node_baton : rev_baton,
                                         found_node,
                                         found_node ? nodepool : revpool));
        }

      /* Is there a text content-block to parse? */
      if ((valstr = apr_hash_get (headers,
                                  SVN_REPOS_DUMPFILE_TEXT_CONTENT_LENGTH,
                                  APR_HASH_KEY_STRING)))
        {
          SVN_ERR (parse_text_block (stream, 
                                     (apr_size_t) atoi (valstr),
                                     parse_fns,
                                     found_node ? node_baton : rev_baton,
                                     buffer, 
                                     buflen,
                                     found_node ? nodepool : revpool));
        }
      
      /* If we just finished processing a node record, we need to
         close the node record and clear the per-node subpool. */
      if (found_node)
        {
          SVN_ERR (parse_fns->close_node (node_baton));
          svn_pool_clear (nodepool);
        }
      
      /*** End of processing for one record. ***/

    } /* end of stream */

  /* Close out whatever revision we're in. */
  if (rev_baton != NULL)
    SVN_ERR (parse_fns->close_revision (rev_baton));

  svn_pool_destroy (linepool);
  svn_pool_destroy (revpool);
  svn_pool_destroy (nodepool);
  return SVN_NO_ERROR;
}



/*----------------------------------------------------------------------*/

/** vtable for doing commits to a fs **/


static struct node_baton *
make_node_baton (apr_hash_t *headers,
                 struct revision_baton *rb,
                 apr_pool_t *pool)
{
  struct node_baton *nb = apr_pcalloc (pool, sizeof(*nb));
  const char *val;

  /* Start with sensible defaults. */
  nb->rb = rb;
  nb->pool = pool;
  nb->kind = svn_node_unknown;

  /* Then add info from the headers.  */
  if ((val = apr_hash_get (headers, SVN_REPOS_DUMPFILE_NODE_PATH,
                           APR_HASH_KEY_STRING)))
    nb->path = apr_pstrdup (pool, val);

  if ((val = apr_hash_get (headers, SVN_REPOS_DUMPFILE_NODE_KIND,
                           APR_HASH_KEY_STRING)))
    {
      if (! strcmp (val, "file"))
        nb->kind = svn_node_file;
      else if (! strcmp (val, "dir"))
        nb->kind = svn_node_dir;
    }

  if ((val = apr_hash_get (headers, SVN_REPOS_DUMPFILE_NODE_ACTION,
                           APR_HASH_KEY_STRING)))
    {
      if (! strcmp (val, "change"))
        nb->action = svn_node_action_change;
      else if (! strcmp (val, "add"))
        nb->action = svn_node_action_add;
      else if (! strcmp (val, "delete"))
        nb->action = svn_node_action_delete;
      else if (! strcmp (val, "replace"))
        nb->action = svn_node_action_replace;
    }

  if ((val = apr_hash_get (headers, SVN_REPOS_DUMPFILE_NODE_COPYFROM_REV,
                           APR_HASH_KEY_STRING)))
    {
      nb->copyfrom_rev = (svn_revnum_t) atoi (val);
    }
  if ((val = apr_hash_get (headers, SVN_REPOS_DUMPFILE_NODE_COPYFROM_PATH,
                           APR_HASH_KEY_STRING)))
    {
      nb->copyfrom_path = apr_pstrdup (pool, val);
    }

  /* What's cool about this dump format is that the parser just
     ignores any unrecognized headers.  :-)  */

  return nb;
}

static struct revision_baton *
make_revision_baton (apr_hash_t *headers,
                     struct parse_baton *pb,
                     apr_pool_t *pool)
{
  struct revision_baton *rb = apr_pcalloc (pool, sizeof(*rb));
  const char *val;

  rb->pb = pb;
  rb->pool = pool;
  rb->rev = SVN_INVALID_REVNUM;

  if ((val = apr_hash_get (headers, SVN_REPOS_DUMPFILE_REVISION_NUMBER,
                           APR_HASH_KEY_STRING)))
    rb->rev = SVN_STR_TO_REV(val);

  return rb;
}


static svn_error_t *
new_revision_record (void **revision_baton,
                     apr_hash_t *headers,
                     void *parse_baton,
                     apr_pool_t *pool)
{
  struct parse_baton *pb = parse_baton;
  struct revision_baton *rb;
  svn_revnum_t head_rev;

  rb = make_revision_baton (headers, pb, pool);
  SVN_ERR (svn_fs_youngest_rev (&head_rev, pb->fs, pool));

  /* Calculate the revision 'offset' for finding copyfrom sources.
     It might be positive or negative. */
  rb->rev_offset = (rb->rev) - (head_rev + 1);

  if (rb->rev > 0)
    {
      /* Create a new fs txn. */
      SVN_ERR (svn_fs_begin_txn (&(rb->txn), pb->fs, head_rev, pool));
      SVN_ERR (svn_fs_txn_root (&(rb->txn_root), rb->txn, pool));
      
      if (pb->outstream)
        svn_stream_printf (pb->outstream, pool,
                           "<<< Started new txn, based on original revision %"
                           SVN_REVNUM_T_FMT "\n", rb->rev);
    }

  /* If we're parsing revision 0, only the revision are (possibly)
     interesting to us: when loading the stream into an empty
     filesystem, then we want new filesystem's revision 0 to have the
     same props.  Otherwise, we just ignore revision 0 in the stream. */
  
  *revision_baton = rb;
  return SVN_NO_ERROR;
}



/* Factorized helper func for new_node_record() */
static svn_error_t *
maybe_add_with_history (struct node_baton *nb,
                        struct revision_baton *rb,
                        apr_pool_t *pool)
{
  struct parse_baton *pb = rb->pb;

  if ((nb->copyfrom_path == NULL) || (! pb->use_history))
    {
      /* Add empty file or dir, without history. */
      if (nb->kind == svn_node_file)
        SVN_ERR (svn_fs_make_file (rb->txn_root, nb->path, pool));

      else if (nb->kind == svn_node_dir)
        SVN_ERR (svn_fs_make_dir (rb->txn_root, nb->path, pool));
    }
  else
    {
      /* Hunt down the source revision in this fs. */
      svn_fs_root_t *copy_root;
      svn_revnum_t src_rev = nb->copyfrom_rev - rb->rev_offset;

      if (! SVN_IS_VALID_REVNUM(src_rev))
        return svn_error_createf (SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                                  "Relative copyfrom_rev %" SVN_REVNUM_T_FMT
                                  " is not available in current repository.",
                                  src_rev);

      SVN_ERR (svn_fs_revision_root (&copy_root, pb->fs, src_rev, pool));
      SVN_ERR (svn_fs_copy (copy_root, nb->copyfrom_path,
                            rb->txn_root, nb->path, pool));

      if (pb->outstream)
        {
          apr_size_t len = 9;
          svn_stream_write (pb->outstream, "COPIED...", &len);
        }
    }

  return SVN_NO_ERROR;
}



static svn_error_t *
new_node_record (void **node_baton,
                 apr_hash_t *headers,
                 void *revision_baton,
                 apr_pool_t *pool)
{
  struct revision_baton *rb = revision_baton;
  struct parse_baton *pb = rb->pb;
  struct node_baton *nb = make_node_baton (headers, rb, pool);

  switch (nb->action)
    {
    case svn_node_action_change:
      {
        if (pb->outstream)
          svn_stream_printf (pb->outstream, pool,
                             "     * editing path : %s ...", nb->path);
        break;
      }
    case svn_node_action_delete:
      {
        if (pb->outstream)
          svn_stream_printf (pb->outstream, pool,
                             "     * deleting path : %s ...", nb->path);
        SVN_ERR (svn_fs_delete_tree (rb->txn_root, nb->path, pool));
        break;
      }
    case svn_node_action_add:
      {
        if (pb->outstream)
          svn_stream_printf (pb->outstream, pool,
                             "     * adding path : %s ...", nb->path);

        SVN_ERR (maybe_add_with_history (nb, rb, pool));
        break;
      }
    case svn_node_action_replace:
      {
        if (pb->outstream)
          svn_stream_printf (pb->outstream, pool,
                             "     * replacing path : %s ...", nb->path);

        SVN_ERR (svn_fs_delete_tree (rb->txn_root, nb->path, pool));

        SVN_ERR (maybe_add_with_history (nb, rb, pool));
        break;
      }
    default:
      return svn_error_createf (SVN_ERR_STREAM_UNRECOGNIZED_DATA, NULL,
                                "Unrecognized node-action on node %s.",
                                nb->path);
    }

  *node_baton = nb;
  return SVN_NO_ERROR;
}


static svn_error_t *
set_revision_property (void *baton,
                       const char *name,
                       const svn_string_t *value)
{
  struct revision_baton *rb = baton;

  if (rb->rev > 0)
    {
      SVN_ERR (svn_fs_change_txn_prop (rb->txn, name, value, rb->pool));
      
      /* Remember any datestamp that passes through!  (See comment in
         close_revision() below.) */
      if (! strcmp (name, SVN_PROP_REVISION_DATE))
        rb->datestamp = svn_string_dup (value, rb->pool);
    }
  else if (rb->rev == 0)
    {     
      /* Special case: set revision 0 properties when loading into an
         'empty' filesystem. */
      struct parse_baton *pb = rb->pb;
      svn_revnum_t youngest_rev;

      SVN_ERR (svn_fs_youngest_rev (&youngest_rev, pb->fs, rb->pool));

      if (youngest_rev == 0)
        SVN_ERR (svn_fs_change_rev_prop (pb->fs, 0, name, value, rb->pool));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
set_node_property (void *baton,
                   const char *name,
                   const svn_string_t *value)
{
  struct node_baton *nb = baton;
  struct revision_baton *rb = nb->rb;

  SVN_ERR (svn_fs_change_node_prop (rb->txn_root, nb->path,
                                    name, value, nb->pool));
  
  return SVN_NO_ERROR;
}


static svn_error_t *
set_fulltext (svn_stream_t **stream,
              void *node_baton)
{
  struct node_baton *nb = node_baton;
  struct revision_baton *rb = nb->rb;

  return svn_fs_apply_text (stream,
                            rb->txn_root, nb->path,
                            nb->pool);
}


static svn_error_t *
close_node (void *baton)
{
  struct node_baton *nb = baton;
  struct revision_baton *rb = nb->rb;
  struct parse_baton *pb = rb->pb;

  if (pb->outstream)
    {
      apr_size_t len = 7;
      svn_stream_write (pb->outstream, " done.\n", &len);
    }
  
  return SVN_NO_ERROR;
}


static svn_error_t *
close_revision (void *baton)
{
  struct revision_baton *rb = baton;
  struct parse_baton *pb = rb->pb;
  const char *conflict_msg = NULL;
  svn_revnum_t new_rev;
  svn_error_t *err;

  if (rb->rev <= 0)
    return SVN_NO_ERROR;

  err = svn_fs_commit_txn (&conflict_msg, &new_rev, rb->txn);

  if (err)
    {
      svn_fs_abort_txn (rb->txn);
      if (conflict_msg)
        return svn_error_quick_wrap (err, conflict_msg);
      else
        return err;
    }

  /* Grrr, svn_fs_commit_txn rewrites the datestamp property to the
     current clock-time.  We don't want that, we want to preserve
     history exactly.  Good thing revision props aren't versioned! */
  if (rb->datestamp)
    SVN_ERR (svn_fs_change_rev_prop (pb->fs, new_rev,
                                     SVN_PROP_REVISION_DATE, rb->datestamp,
                                     rb->pool));

  if (pb->outstream)
    svn_stream_printf (pb->outstream, rb->pool,
                       "\n------- Committed new rev %" SVN_REVNUM_T_FMT
                       " (loaded from original rev %" SVN_REVNUM_T_FMT
                       ") >>>\n\n", new_rev, rb->rev);

  return SVN_NO_ERROR;
}


/*----------------------------------------------------------------------*/

/** The public routines **/


svn_error_t *
svn_repos_get_fs_build_parser (const svn_repos_parser_fns_t **parser_callbacks,
                               void **parse_baton,
                               svn_repos_t *repos,
                               svn_boolean_t use_history,
                               svn_stream_t *outstream,
                               apr_pool_t *pool)
{
  svn_repos_parser_fns_t *parser = apr_pcalloc (pool, sizeof(*parser));
  struct parse_baton *pb = apr_pcalloc (pool, sizeof(*pb));

  parser->new_revision_record = new_revision_record;
  parser->new_node_record = new_node_record;
  parser->set_revision_property = set_revision_property;
  parser->set_node_property = set_node_property;
  parser->set_fulltext = set_fulltext;
  parser->close_node = close_node;
  parser->close_revision = close_revision;

  pb->repos = repos;
  pb->fs = svn_repos_fs (repos);
  pb->use_history = use_history;
  pb->outstream = outstream;

  *parser_callbacks = parser;
  *parse_baton = pb;
  return SVN_NO_ERROR;
}



svn_error_t *
svn_repos_load_fs (svn_repos_t *repos,
                   svn_stream_t *dumpstream,
                   svn_stream_t *feedback_stream,
                   apr_pool_t *pool)
{
  const svn_repos_parser_fns_t *parser;
  void *parse_baton;
  
  /* This is really simple. */  

  SVN_ERR (svn_repos_get_fs_build_parser (&parser, &parse_baton,
                                          repos,
                                          TRUE, /* look for copyfrom revs */
                                          feedback_stream,
                                          pool));

  SVN_ERR (svn_repos_parse_dumpstream (dumpstream, parser, parse_baton, pool));

  return SVN_NO_ERROR;
}
