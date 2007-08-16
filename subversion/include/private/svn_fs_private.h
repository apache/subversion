/*
 * svn_fs_private.h: Private declarations for the filesystem layer to
 * be consumed by libsvn_fs* and non-libsvn_fs* modules.
 *
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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

#ifndef SVN_FS_PRIVATE_H
#define SVN_FS_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* The maximum length of a transaction name.  The Berkeley DB backend
   generates transaction names from a sequence expressed as a base 36
   number with a maximum of MAX_KEY_SIZE (currently 200) bytes.  The
   FSFS backend generates transaction names of the form
   <hostname>-<pid>-<time>-<uniquifier>, where <uniquifier> runs from
   0-99999 (see create_txn_dir() in fs_fs.c).  The maximum length is
   APRMAXHOSTLEN + 39:
     APRMAXHOSTLEN -> hostname
     1             -> -
     10            -> 32 bit process ID
     1             -> -
     20            -> 64 bit time
     1             -> -
     5             -> 0-99999
     1             -> trailing \0

   Use APRMAXHOSTLEN + 63 just to have some extra space.
 */
#define SVN_FS__TXN_MAX_LEN (APRMAXHOSTLEN+63)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_FS_PRIVATE_H */
