/* fs_skels.c --- conversion between fs native types and skeletons
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

#include "fs_skels.h"
#include "svn_error.h"
#include "string.h"
#include "svn_string.h"
#include "skel.h"
#include "id.h"
#include "validate.h"


static svn_error_t *
skel_err (const char *skel_type,
          apr_pool_t *pool)
{
  return svn_error_createf (SVN_ERR_FS_MALFORMED_SKEL, 0, NULL, pool, 
                            "Malformed%s%s skeleton", 
                            skel_type ? " " : "",
                            skel_type ? skel_type : "");
}



/*** Validity Checking ***/

static int 
is_valid_proplist_skel (skel_t *skel)
{
  int len = svn_fs__list_length (skel);

  if (len >= 0
      && (len & 1) == 0)
    {
      skel_t *elt;

      for (elt = skel->children; elt; elt = elt->next)
        if (! elt->is_atom)
          return 0;

      return 1;
    }

  return 0;
}


static int
is_valid_revision_skel (skel_t *skel)
{
  int len = svn_fs__list_length (skel);

  if (len == 3)
    {
      if (svn_fs__matches_atom (skel->children, "revision")
          && skel->children->next != NULL
          && is_valid_proplist_skel (skel->children->next->next))
        {
          skel_t *id = skel->children->next;
          if (id->is_atom
              && 0 == (1 & svn_fs__count_id_components (id->data, id->len)))
            return 1;
        }
    }

  return 0;
}


static int
is_valid_transaction_skel (skel_t *skel)
{
  int len = svn_fs__list_length (skel);

  if (len == 4
      && svn_fs__matches_atom (skel->children, "transaction")
      && skel->children->next->is_atom
      && skel->children->next->next->is_atom
      && svn_fs__is_valid_proplist (skel->children->next->next->next))
    return 1;

  return 0;
}


static int 
is_valid_representation_skel (skel_t *skel)
{
  int len = svn_fs__list_length (skel);

  /* ### TODO:  This is *really* weak validity checking! */
  if ((len >= 2)
      && (! skel->children->is_atom)
      && (svn_fs__list_length (skel->children) >= 1))
    {
      if (svn_fs__matches_atom (skel->children->children, "fulltext"))
        {
          return 1;
        }
      if (svn_fs__matches_atom (skel->children->children, "delta"))
        {
          return 1;
        }
    }

  return 0;
}



/*** Parsing (conversion from skeleton to native FS type) ***/

svn_error_t *
svn_fs__parse_proplist_skel (apr_hash_t **proplist_p,
                             skel_t *skel,
                             apr_pool_t *pool)
{
  apr_hash_t *proplist = NULL;
  skel_t *elt;

  /* Validate the skel. */
  if (! is_valid_proplist_skel (skel))
    return skel_err ("proplist", pool);
  
  /* Create the returned structure */
  if (skel->children)
    proplist = apr_hash_make (pool);
  for (elt = skel->children; elt; elt = elt->next->next)
    {
      svn_string_t *value = svn_string_ncreate (elt->next->data, 
                                                elt->next->len, pool);
      apr_hash_set (proplist, elt->data, elt->len, (void *)value);
    }

  /* Return the structure. */
  *proplist_p = proplist;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__parse_revision_skel (svn_fs__revision_t **revision_p, 
                             skel_t *skel,
                             apr_pool_t *pool)
{
  svn_fs__revision_t *revision;
  skel_t *id, *proplist;

  /* Validate the skel. */
  if (! is_valid_revision_skel (skel))
    return skel_err ("revision", pool);
  id = skel->children->next;
  proplist = skel->children->next->next;

  /* Create the returned structure */
  revision = apr_pcalloc (pool, sizeof (*revision));
  revision->id = svn_fs_parse_id (id->data, id->len, pool);
  SVN_ERR (svn_fs__parse_proplist_skel (&(revision->proplist), 
                                        proplist, pool));
  
  /* Return the structure. */
  *revision_p = revision;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__parse_transaction_skel (svn_fs__transaction_t **transaction_p, 
                                skel_t *skel,
                                apr_pool_t *pool)
{
  svn_fs__transaction_t *transaction;
  skel_t *root_id, *base_root_id, *proplist;
  
  /* Validate the skel. */
  if (! is_valid_transaction_skel (skel))
    return skel_err ("transaction", pool);
  root_id = skel->children->next;
  base_root_id = skel->children->next->next;
  proplist = skel->children->next->next->next;

  /* Create the returned structure */
  transaction = apr_pcalloc (pool, sizeof (*transaction));
  transaction->root_id = svn_fs_parse_id (root_id->data, root_id->len, pool);
  transaction->base_root_id = svn_fs_parse_id (base_root_id->data,
                                               base_root_id->len, pool);
  SVN_ERR (svn_fs__parse_proplist_skel (&(transaction->proplist), 
                                        proplist, pool));
  
  /* Return the structure. */
  *transaction_p = transaction;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__parse_representation_skel (svn_fs__representation_t **rep_p,
                                   skel_t *skel,
                                   apr_pool_t *pool)
{
  svn_fs__representation_t *rep;
  skel_t *header_skel;

  /* Validate the skel. */
  if (! is_valid_representation_skel (skel))
    return skel_err ("representation", pool);
  header_skel = skel->children;

  /* Create the returned structure */
  rep = apr_pcalloc (pool, sizeof (*rep));
  
  /* KIND */
  if (svn_fs__matches_atom (header_skel->children, "fulltext"))
    rep->kind = svn_fs__rep_kind_fulltext;
  else
    rep->kind = svn_fs__rep_kind_delta;

  /* FLAG ... ("mutable" is the only supported one) */
  {
    skel_t *flag = header_skel->children->next;
    while (flag)
      {
        if (svn_fs__matches_atom (flag, "mutable"))
          {
            rep->is_mutable = TRUE;
            break;
          }
        flag = flag->next;
      }
  }

  /* KIND-SPECIFIC stuff */
  if (rep->kind == svn_fs__rep_kind_fulltext)
    {
      /* "fulltext"-specific. */
      rep->contents.fulltext.string_key 
        = apr_pstrndup (pool, 
                        skel->children->next->data,
                        skel->children->next->len);
    }
  else
    {
      /* "delta"-specific. */
      skel_t *chunk_skel = skel->children->next;
      svn_fs__rep_delta_chunk_t *chunk;
      svn_fs__rep_delta_window_t *window;
      apr_array_header_t *chunks;
      
      /* Alloc the chunk array. */
      chunks = apr_array_make (pool, svn_fs__list_length (skel) - 1, 
                               sizeof (chunk));

      /* Process the chunks. */
      while (chunk_skel)
        {
          skel_t *window_skel = chunk_skel->children->next;
          skel_t *diff_skel = window_skel->children;
          skel_t *checksum_skel = window_skel->children->next->next;

          /* Allocate a chunk and its window */
          chunk = apr_palloc (pool, sizeof (*chunk));
          window = apr_palloc (pool, sizeof (*window));

          /* Populate the window */
          window->string_key = apr_pstrndup (pool,
                                             diff_skel->children->next->data,
                                             diff_skel->children->next->len);
          window->size = atoi (apr_pstrndup (pool,
                                             diff_skel->next->data,
                                             diff_skel->next->len));
          memcpy (&(window->checksum), checksum_skel->children->data, 
                  MD5_DIGESTSIZE);
          window->rep_key = apr_pstrndup (pool, 
                                          checksum_skel->next->data,
                                          checksum_skel->next->len);

          /* Add this chunk to the array. */
          chunk->window = window;
          chunk->offset = atoi (apr_pstrndup (pool, 
                                              chunk_skel->children->data,
                                              chunk_skel->children->len));
          (*((svn_fs__rep_delta_chunk_t **)(apr_array_push (chunks)))) = chunk;

          /* Next... */
          chunk_skel = chunk_skel->next;
        }

      /* Add the chunks array to the representation. */
      rep->contents.delta.chunks = chunks;
    }

  /* Return the structure. */
  *rep_p = rep;
  return SVN_NO_ERROR;
}



/*** Unparsing (conversion from native FS type to skeleton) ***/

svn_error_t *
svn_fs__unparse_proplist_skel (skel_t **skel_p,
                               apr_hash_t *proplist,
                               apr_pool_t *pool)
{
  skel_t *skel = svn_fs__make_empty_list (pool);
  apr_hash_index_t *hi;

  /* Create the skel. */
  if (proplist)
    {
      /* Loop over hash entries */
      for (hi = apr_hash_first (pool, proplist); hi; hi = apr_hash_next (hi))
        {
          const void *key;
          void *val;
          apr_ssize_t klen;
          svn_string_t *value;
          
          apr_hash_this (hi, &key, &klen, &val);
          value = val;
          
          /* VALUE */
          svn_fs__prepend (svn_fs__mem_atom (value->data, value->len, pool), 
                           skel);
          
          /* NAME */
          svn_fs__prepend (svn_fs__mem_atom (key, klen, pool), skel);
        }
    }
     
  /* Validate and return the skel. */
  if (! is_valid_proplist_skel (skel))
    return skel_err ("proplist", pool);
  *skel_p = skel;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__unparse_revision_skel (skel_t **skel_p,
                               svn_fs__revision_t *revision,
                               apr_pool_t *pool)
{
  skel_t *skel;
  skel_t *proplist_skel;
  svn_stringbuf_t *id_str;

  /* Create the skel. */
  skel = svn_fs__make_empty_list (pool);

  /* PROPLIST */
  svn_fs__unparse_proplist_skel (&proplist_skel, revision->proplist, pool);
  svn_fs__prepend (proplist_skel, skel);

  /* ID */
  id_str = svn_fs_unparse_id (revision->id, pool);
  svn_fs__prepend (svn_fs__mem_atom (id_str->data, id_str->len, pool), skel);

  /* "revision" */
  svn_fs__prepend (svn_fs__str_atom ("revision", pool), skel);

  /* Validate and return the skel. */
  if (! is_valid_revision_skel (skel))
    return skel_err ("revision", pool);
  *skel_p = skel;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__unparse_transaction_skel (skel_t **skel_p,
                                  svn_fs__transaction_t *transaction,
                                  apr_pool_t *pool)
{
  skel_t *skel;
  skel_t *proplist_skel;
  svn_stringbuf_t *id_str;

  /* Create the skel. */
  skel = svn_fs__make_empty_list (pool);

  /* PROPLIST */
  svn_fs__unparse_proplist_skel (&proplist_skel, transaction->proplist, pool);
  svn_fs__prepend (proplist_skel, skel);

  /* BASE-ROOT-ID */
  id_str = svn_fs_unparse_id (transaction->base_root_id, pool);
  svn_fs__prepend (svn_fs__mem_atom (id_str->data, id_str->len, pool), skel);

  /* ROOT-ID */
  id_str = svn_fs_unparse_id (transaction->root_id, pool);
  svn_fs__prepend (svn_fs__mem_atom (id_str->data, id_str->len, pool), skel);

  /* "transaction" */
  svn_fs__prepend (svn_fs__str_atom ("transaction", pool), skel);

  /* Validate and return the skel. */
  if (! is_valid_transaction_skel (skel))
    return skel_err ("transaction", pool);
  *skel_p = skel;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__unparse_representation_skel (skel_t **skel_p,
                                     svn_fs__representation_t *rep,
                                     apr_pool_t *pool)
{
  skel_t *skel;
  skel_t *header_skel;

  /* Create the skel. */
  skel = svn_fs__make_empty_list (pool);

  if (rep->kind == svn_fs__rep_kind_fulltext)
    {
      /*** Fulltext Representation. ***/

      /* Create the header. */
      header_skel = svn_fs__make_empty_list (pool);
      
      /* STRING-KEY */
      if ((! rep->contents.fulltext.string_key) 
          || (! *rep->contents.fulltext.string_key))
        svn_fs__prepend (svn_fs__mem_atom (NULL, 0, pool), skel);
      else
        svn_fs__prepend (svn_fs__str_atom 
                         (rep->contents.fulltext.string_key, pool), skel);
      
      /* "mutable" flag (optional) */
      if (rep->is_mutable)
        svn_fs__prepend (svn_fs__str_atom ("mutable", pool), header_skel);
      
      /* "fulltext" */
      svn_fs__prepend (svn_fs__str_atom ("fulltext", pool), header_skel);

      /* header */
      svn_fs__prepend (header_skel, skel);
    }
  else if (rep->kind == svn_fs__rep_kind_delta)
    {
      /*** Delta Representation. ***/
      int i;
      apr_array_header_t *chunks = rep->contents.delta.chunks;

      /* Loop backwards through the windows, creating and prepending skels. */
      for (i = chunks->nelts; i > 0; i--)
        {
          skel_t *window_skel = svn_fs__make_empty_list (pool);
          skel_t *chunk_skel = svn_fs__make_empty_list (pool);
          skel_t *diff_skel = svn_fs__make_empty_list (pool);
          skel_t *checksum_skel = svn_fs__make_empty_list (pool);
          const char *size_str;
          const char *offset_str;
          svn_fs__rep_delta_chunk_t *chunk = 
            (((svn_fs__rep_delta_chunk_t **) chunks->elts)[i - 1]);
          svn_fs__rep_delta_window_t *window = chunk->window;

          /* OFFSET */
          offset_str = apr_psprintf (pool, "%" APR_SIZE_T_FMT,
                                     chunk->offset);

          /* SIZE */
          size_str = apr_psprintf (pool, "%" APR_SIZE_T_FMT, 
                                   chunk->window->size);

          /* DIFF */
          if ((! window->string_key) || (! *window->string_key))
            svn_fs__prepend (svn_fs__mem_atom (NULL, 0, pool), diff_skel);
          else
            svn_fs__prepend (svn_fs__str_atom (window->string_key,
                                               pool), diff_skel);
          svn_fs__prepend (svn_fs__str_atom ("svndiff", pool), diff_skel);
        
          /* CHECKSUM */
          svn_fs__prepend (svn_fs__mem_atom (window->checksum,
                                             MD5_DIGESTSIZE / 
                                             sizeof (*window->checksum), 
                                             pool), checksum_skel);
          svn_fs__prepend (svn_fs__str_atom ("md5", pool), checksum_skel);
          
          /* REP-KEY */
          if ((! window->rep_key) || (! *window->rep_key))
            svn_fs__prepend (svn_fs__mem_atom (NULL, 0, pool), window_skel);
          else
            svn_fs__prepend (svn_fs__str_atom (window->rep_key, 
                                               pool), window_skel);
          svn_fs__prepend (checksum_skel, window_skel);
          svn_fs__prepend (svn_fs__str_atom (size_str, pool), window_skel);
          svn_fs__prepend (diff_skel, window_skel);
          
          /* window header. */
          svn_fs__prepend (window_skel, chunk_skel);
          svn_fs__prepend (svn_fs__str_atom (offset_str, pool), chunk_skel);
          
          /* Add this window item to the main skel. */
          svn_fs__prepend (chunk_skel, skel);
        }
      
      /* Create the header. */
      header_skel = svn_fs__make_empty_list (pool);
      
      /* "mutable" flag (optional) */
      if (rep->is_mutable)
        svn_fs__prepend (svn_fs__str_atom ("mutable", pool), header_skel);
      
      /* "delta" */
      svn_fs__prepend (svn_fs__str_atom ("delta", pool), header_skel);

      /* header */
      svn_fs__prepend (header_skel, skel);
    }
  else
    abort();

  /* Validate and return the skel. */
  if (! is_valid_representation_skel (skel))
    return skel_err ("representation", pool);
  *skel_p = skel;
  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
