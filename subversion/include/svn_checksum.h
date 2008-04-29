/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2007-2008 CollabNet.  All rights reserved.
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
 * @endcopyright
 *
 * @file svn_checksum.h
 * @brief Subversion checksum routines
 */

#ifndef SVN_CHECKSUM_H
#define SVN_CHECKSUM_H

#include "svn_pools.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/**
 * Various types of checksums.
 *
 * @since New in 1.6.
 */
typedef enum
{
  /** The checksum is (or should be set to) an MD5 checksum. */
  svn_checksum_md5,

  /** The checksum is (or should be set to) a SHA1 checksum. */
  svn_checksum_sha1
} svn_checksum_kind_t;

/**
 * A generic checksum representation.
 *
 * @since New in 1.6.
 */
typedef struct svn_checksum_t
{
  /** The bytes of the checksum. */
  unsigned char *digest;

  /** The type of the checksum.  This should never be changed by consumers
      of the APIs. */
  svn_checksum_kind_t kind;

  /** The pool to use for new digest allocations.  It should match the
      pool that this checksum was allocated from. */
  apr_pool_t *pool;
} svn_checksum_t;

/**
 * Opaque type for creating checksums of data.
 */
typedef struct svn_checksum_ctx_t svn_checksum_ctx_t;

/** Allocate, initialize and return a @c svn_checksum_t structure of type
 * @a kind.  The checksum is allocated in @a pool.
 *
 * @since New in 1.6.
 */
svn_checksum_t *
svn_checksum_create(svn_checksum_kind_t kind,
                    apr_pool_t *pool);

/** Set @c checksum->digest to all zeros, which, by convention, matches
 * all other checksums.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_checksum_clear(svn_checksum_t *checksum);

/** Compare digests @a d1 and @a d2.  If their kinds do not match or
 * if neither is all zeros, and their content does not match, then
 * return FALSE; else return TRUE.
 *
 * @since New in 1.6.
 */
svn_boolean_t
svn_checksum_match(svn_checksum_t *d1,
                   svn_checksum_t *d2);


/**
 * Do a deep copy of @a src to @a dest.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_checksum_dup(svn_checksum_t *dest,
                 svn_checksum_t *src);


/** Return the hex representation of @a checksum, allocating the string
 * in @a pool.
 *
 * @since New in 1.6.
 */
const char *
svn_checksum_to_cstring_display(svn_checksum_t *checksum,
                                apr_pool_t *pool);


/** Return the hex representation of @a checksum, allocating the string
 * in @a pool.  If @a digest is all zeros, then return NULL.
 *
 * @since New in 1.6.
 */
const char *
svn_checksum_to_cstring(svn_checksum_t *checksum,
                        apr_pool_t *pool);


/** Parse the hex representation of a checksum into @checksum.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_checksum_parse_hex(svn_checksum_t *checksum,
                       const char *hex);

/**
 * Return in @a *checksum the checksum of type @a kind for the bytes beginning
 * at @a data, and going for @a len.  @a *checksum is allocated in @a pool.
 */
svn_error_t *
svn_checksum(svn_checksum_t **checksum,
             svn_checksum_kind_t kind,
             const void *data,
             apr_size_t len,
             apr_pool_t *pool);


/**
 * Return in @a pool a newly allocated checksum populated with the checksum
 * of type @a kind for the empty string of type.
 */
svn_checksum_t *
svn_checksum_empty_checksum(svn_checksum_kind_t kind,
                            apr_pool_t *pool);


/**
 * Create a new @c svn_checksum_ctx_t structure, allocated from @a pool for
 * calculating checksums of type @a checksum->kind.  When the checksum is
 * finalized with svn_checksum_final(), the result will be placed in
 * @a checksum->digest.
 *
 * @since New in 1.6.
 */
svn_checksum_ctx_t *
svn_checksum_ctx_create(svn_checksum_t *checksum,
                        apr_pool_t *pool);

/**
 * Update the checksum represented by @a ctx, with @a len bytes starting at
 * @a data.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_checksum_update(svn_checksum_ctx_t *ctx,
                    const void *data,
                    apr_size_t len);


/**
 * Finalize the checksum used when creating @a ctx.  The digest will be copied
 * into the digest field of the checksum used when svn_checksum_ctx_create()
 * was called.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_checksum_final(svn_checksum_ctx_t *ctx);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CHECKSUM_H */
