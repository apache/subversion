/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
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
 * @file svn_client_private.h
 * @brief Private functions at the Subversion client layer.
 */

#ifndef SVN_CLIENT_PRIVATE_H
#define SVN_CLIENT_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Write a property as an XML element into @a *outstr.
 *
 * If @a outstr is NULL, allocate @a *outstr in @a pool; else append to
 * @a *outstr, allocating in @a outstr's pool
 *
 * @a propname is the property name. @a propval is the property value, which
 * will be encoded it if contains unsafe bytes.
 *
 * @since New in 1.6.
 *
 * This is a private API for Subversion's own use.
 */
void
svn_client__print_xml_prop(svn_stringbuf_t **outstr,
                           const char *propname,
                           svn_string_t *propval,
                           apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CLIENT_PRIVATE_H */
