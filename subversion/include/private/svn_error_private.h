/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_error_private.h
 * @brief Common exception handling for Subversion - Internal parts
 */




#ifndef SVN_ERROR_PRIVATE_H
#define SVN_ERROR_PRIVATE_H


/** Set the error location for debug mode. */
void svn_error__locate(const char *file, long line);


#endif /* SVN_ERROR_PRIVATE_H */
