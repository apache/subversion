/*
 * entries.h :  manipulating entries
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


#ifndef SVN_LIBSVN_WC_ENTRIES_H
#define SVN_LIBSVN_WC_ENTRIES_H

#include <apr_pools.h>
#include "svn_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define SVN_WC__ENTRIES_TOPLEVEL \
        "\x77\x63\x2d\x65\x6e\x74\x72\x69\x65\x73"
        /* "wc-entries" */

#define SVN_WC__ENTRIES_ENTRY \
        "\x65\x6e\x74\x72\x79"
        /* "entry" */

/* String representations for svn_node_kind.  This maybe should be
   abstracted farther out? */
#define SVN_WC__ENTRIES_ATTR_FILE_STR \
        "\x66\x69\x6c\x65"
        /* "file" */

#define SVN_WC__ENTRIES_ATTR_DIR_STR \
        "\x64\x69\x72"
        /* "dir" */


/* The names of the XML attributes for storing entries' information.
   ### If you add or remove items here, you probably want to make sure
   to do the same for the SVN_WC__ENTRY_MODIFY_* #defines as well. */
#define SVN_WC__ENTRY_ATTR_NAME \
        "\x6e\x61\x6d\x65"
        /* "name" */

#define SVN_WC__ENTRY_ATTR_REVISION \
        "\x72\x65\x76\x69\x73\x69\x6f\x6e"
        /* "revision" */

#define SVN_WC__ENTRY_ATTR_URL \
        "\x75\x72\x6c"
        /* "url" */

#define SVN_WC__ENTRY_ATTR_REPOS \
        "\x72\x65\x70\x6f\x73"
        /* "repos" */
        
#define SVN_WC__ENTRY_ATTR_KIND \
        "\x6b\x69\x6e\x64"
        /* "kind" */

#define SVN_WC__ENTRY_ATTR_TEXT_TIME \
        "\x74\x65\x78\x74\x2d\x74\x69\x6d\x65"
        /* "text-time" */

#define SVN_WC__ENTRY_ATTR_PROP_TIME \
        "\x70\x72\x6f\x70\x2d\x74\x69\x6d\x65"
        /* "prop-time" */

#define SVN_WC__ENTRY_ATTR_CHECKSUM \
        "\x63\x68\x65\x63\x6b\x73\x75\x6d"
        /* "checksum" */

#define SVN_WC__ENTRY_ATTR_SCHEDULE \
        "\x73\x63\x68\x65\x64\x75\x6c\x65"
        /* "schedule" */

#define SVN_WC__ENTRY_ATTR_COPIED \
        "\x63\x6f\x70\x69\x65\x64"
        /* "copied" */

#define SVN_WC__ENTRY_ATTR_DELETED \
        "\x64\x65\x6c\x65\x74\x65\x64"
        /* "deleted" */

#define SVN_WC__ENTRY_ATTR_ABSENT \
        "\x61\x62\x73\x65\x6e\x74"
        /* "absent" */

#define SVN_WC__ENTRY_ATTR_COPYFROM_URL \
        "\x63\x6f\x70\x79\x66\x72\x6f\x6d\x2d\x75\x72\x6c"
        /* "copyfrom-url" */

#define SVN_WC__ENTRY_ATTR_COPYFROM_REV \
        "\x63\x6f\x70\x79\x66\x72\x6f\x6d\x2d\x72\x65\x76"
        /* "copyfrom-rev" */

#define SVN_WC__ENTRY_ATTR_CONFLICT_OLD \
        "\x63\x6f\x6e\x66\x6c\x69\x63\x74\x2d\x6f\x6c\x64"
        /* "conflict-old" - saved old file */

#define SVN_WC__ENTRY_ATTR_CONFLICT_NEW \
        "\x63\x6f\x6e\x66\x6c\x69\x63\x74\x2d\x6e\x65\x77"
        /* "conflict-new" - saved new file */

#define SVN_WC__ENTRY_ATTR_CONFLICT_WRK \
        "\x63\x6f\x6e\x66\x6c\x69\x63\x74\x2d\x77\x72\x6b"
        /* "conflict-wrk" - saved wrk file */

#define SVN_WC__ENTRY_ATTR_PREJFILE \
        "\x70\x72\x6f\x70\x2d\x72\x65\x6a\x65\x63\x74\x2d\x66\x69\x6c\x65"
        /* "prop-reject-file" */

#define SVN_WC__ENTRY_ATTR_CMT_REV \
        "\x63\x6f\x6d\x6d\x69\x74\x74\x65\x64\x2d\x72\x65\x76"
        /* "committed-rev" */

#define SVN_WC__ENTRY_ATTR_CMT_DATE \
        "\x63\x6f\x6d\x6d\x69\x74\x74\x65\x64\x2d\x64\x61\x74\x65"
        /* "committed-date" */

#define SVN_WC__ENTRY_ATTR_CMT_AUTHOR \
        "\x6c\x61\x73\x74\x2d\x61\x75\x74\x68\x6f\x72"
        /* "last-author" */

#define SVN_WC__ENTRY_ATTR_UUID \
        "\x75\x75\x69\x64"
        /* "uuid" */

#define SVN_WC__ENTRY_ATTR_INCOMPLETE \
        "\x69\x6e\x63\x6f\x6d\x70\x6c\x65\x74\x65"
        /* "incomplete" */

#define SVN_WC__ENTRY_ATTR_LOCK_TOKEN \
        "\x6c\x6f\x63\x6b\x2d\x74\x6f\x6b\x65\x6e"
        /* "lock-token" */

#define SVN_WC__ENTRY_ATTR_LOCK_OWNER \
        "\x6c\x6f\x63\x6b\x2d\x6f\x77\x6e\x65\x72"
        /* "lock-owner" */

#define SVN_WC__ENTRY_ATTR_LOCK_COMMENT \
        "\x6c\x6f\x63\x6b\x2d\x63\x6f\x6d\x6d\x65\x6e\x74"
        /* "lock-comment" */

#define SVN_WC__ENTRY_ATTR_LOCK_CREATION_DATE \
        "\x6c\x6f\x63\x6b\x2d\x63\x72\x65\x61\x74\x69\x6f\x6e\x2d\x64\x61" \
        "\x74\x65"
        /* "lock-creation-date" */

/* Attribute values for 'schedule' */
#define SVN_WC__ENTRY_VALUE_ADD \
        "\x61\x64\x64"
        /* "add" */

#define SVN_WC__ENTRY_VALUE_DELETE \
        "\x64\x65\x6c\x65\x74\x65"
        /* "delete" */

#define SVN_WC__ENTRY_VALUE_REPLACE \
        "\x72\x65\x70\x6c\x61\x63\x65"
        /* "replace" */



/* Initialize an entries file based on URL at INITIAL_REV, in the adm
   area for PATH.  The adm area must not already have an entries
   file.  UUID is the repository UUID, and may be NULL.  REPOS is the
   repository root URL and, if not NULL, must be a prefix of URL.

   If initial rev is valid and non-zero, then mark the 'this_dir'
   entry as being incomplete.
*/
svn_error_t *svn_wc__entries_init (const char *path,
                                   const char *uuid,
                                   const char *url,
                                   const char *repos,
                                   svn_revnum_t initial_rev,
                                   apr_pool_t *pool);


/* Create or overwrite an `entries' file for ADM_ACCESS using the contents
   of ENTRIES.  See also svn_wc_entries_read() in the public api. */
svn_error_t *svn_wc__entries_write (apr_hash_t *entries,
                                    svn_wc_adm_access_t *adm_access,
                                    apr_pool_t *pool);


/* Set *NEW_ENTRY to a new entry, taking attributes from ATTS, whose
   keys and values are both char *.  Allocate the entry itself in
   POOL, but don't copy attributes into POOL.  Instead,
   (*NEW_ENTRY)->attributes and any allocated members in *NEW_ENTRY
   will refer directly to ATTS and its values.

   Set MODIFY_FLAGS to reflect the fields that were present in ATTS. */
svn_error_t *svn_wc__atts_to_entry (svn_wc_entry_t **new_entry,
                                    apr_uint32_t *modify_flags,
                                    apr_hash_t *atts,
                                    apr_pool_t *pool);


/* The MODIFY_FLAGS that tell svn_wc__entry_modify which parameters to
   pay attention to.  ### These should track the changes made to the
   SVN_WC__ENTRY_ATTR_* #defines! */
#define SVN_WC__ENTRY_MODIFY_REVISION           0x00000001
#define SVN_WC__ENTRY_MODIFY_URL                0x00000002
#define SVN_WC__ENTRY_MODIFY_REPOS              0x00000004
#define SVN_WC__ENTRY_MODIFY_KIND               0x00000008
#define SVN_WC__ENTRY_MODIFY_TEXT_TIME          0x00000010
#define SVN_WC__ENTRY_MODIFY_PROP_TIME          0x00000020
#define SVN_WC__ENTRY_MODIFY_CHECKSUM           0x00000040
#define SVN_WC__ENTRY_MODIFY_SCHEDULE           0x00000080
#define SVN_WC__ENTRY_MODIFY_COPIED             0x00000100
#define SVN_WC__ENTRY_MODIFY_DELETED            0x00000200
#define SVN_WC__ENTRY_MODIFY_COPYFROM_URL       0x00000400
#define SVN_WC__ENTRY_MODIFY_COPYFROM_REV       0x00000800
#define SVN_WC__ENTRY_MODIFY_CONFLICT_OLD       0x00001000
#define SVN_WC__ENTRY_MODIFY_CONFLICT_NEW       0x00002000
#define SVN_WC__ENTRY_MODIFY_CONFLICT_WRK       0x00004000
#define SVN_WC__ENTRY_MODIFY_PREJFILE           0x00008000
#define SVN_WC__ENTRY_MODIFY_CMT_REV            0x00010000
#define SVN_WC__ENTRY_MODIFY_CMT_DATE           0x00020000
#define SVN_WC__ENTRY_MODIFY_CMT_AUTHOR         0x00040000
#define SVN_WC__ENTRY_MODIFY_UUID               0x00080000
#define SVN_WC__ENTRY_MODIFY_INCOMPLETE         0x00100000
#define SVN_WC__ENTRY_MODIFY_ABSENT             0x00200000
#define SVN_WC__ENTRY_MODIFY_LOCK_TOKEN         0x00400000
#define SVN_WC__ENTRY_MODIFY_LOCK_OWNER         0x00800000
#define SVN_WC__ENTRY_MODIFY_LOCK_COMMENT       0x01000000
#define SVN_WC__ENTRY_MODIFY_LOCK_CREATION_DATE 0x02000000


/* ...or perhaps this to mean all of those above... */
#define SVN_WC__ENTRY_MODIFY_ALL           0x7FFFFFFF

/* ...ORed together with this to mean "I really mean this, don't be
   trying to protect me from myself on this one." */
#define SVN_WC__ENTRY_MODIFY_FORCE         0x80000000


/* Modify an entry for NAME in access baton PATH by folding ("merging")
   in changes, and sync those changes to disk.  New values for the
   entry are pulled from their respective fields in ENTRY, and
   MODIFY_FLAGS is a bitmask to specify which of those field to pay
   attention to.  ADM_ACCESS must hold a write lock.

   - ENTRY->kind specifies the node kind for this entry, and is
     *required* to be set to one of the following valid values:
     'svn_node_dir', 'svn_node_file'.

   - NAME can be NULL to specify that the caller wishes to modify the
     "this dir" entry in PATH.

   If DO_SYNC is FALSE then the modification will be entirely local to the
   access baton, if DO_SYNC is TRUE the modification will be written to
   the entries file.  Be careful when setting DO_SYNC to FALSE, if there
   is no subsequent svn_wc__entries_write call the modifications will be
   lost when the access baton is closed.

   Perform all allocations in POOL.

   NOTE: when you call this function, the entries file will be read,
   tweaked and finally, if DO_SYNC is TRUE, written back out.  */
svn_error_t *svn_wc__entry_modify (svn_wc_adm_access_t *adm_access,
                                   const char *name,
                                   svn_wc_entry_t *entry,
                                   apr_uint32_t modify_flags,
                                   svn_boolean_t do_sync,
                                   apr_pool_t *pool);

/* Remove entry NAME from ENTRIES, unconditionally. */
void svn_wc__entry_remove (apr_hash_t *entries, const char *name);


/* Tweak the entry NAME within hash ENTRIES.  If NEW_URL is non-null,
 * make this the entry's new url.  If NEW_REV is valid, make this the
 * entry's working revision.  (This is purely an in-memory operation.)
 * If REPOS is non-NULL, set the repository root on the entry to REPOS,
 * provided it is a prefix of the entry's URL (and if it is the THIS_DIR
 * entry, all child URLs also match.)
 * If ALLOW_REMOVAL is TRUE the tweaks might cause the entry NAME to
 * be removed from the hash, if ALLOW_REMOVAL is FALSE this will not
 * happen.
 *
 * *WRITE_REQUIRED will be set to TRUE if the tweaks make changes that
 * require the entries to be written to disk, otherwise *WRITE_REQUIRED
 * will not be altered.
 *
 * (Intended as a helper to svn_wc__do_update_cleanup, which see.) 
 */
svn_error_t *
svn_wc__tweak_entry (apr_hash_t *entries,
                     const char *name,
                     const char *new_url,
                     const char *repos,
                     svn_revnum_t new_rev,
                     svn_boolean_t allow_removal,
                     svn_boolean_t *write_required,
                     apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_ENTRIES_H */
