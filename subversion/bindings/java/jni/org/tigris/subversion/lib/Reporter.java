package org.tigris.subversion.lib;

/**
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
 *
 * FIXME: all of the comment is hand-copied-and-pasted comment cut out
 * of the c language header "svn_ra.h". So a lot of the naming
 * convetions in the comment do still relate to the c function names.
 * There should be a preprocessing step which takes the c header file,
 * does naming and type conversions (using translation tables) and then
 * generates this file.
 *
 * Remark: this class corresponds to the subversion c api type
 * 'svn_ra_reporter_t'
 */

import org.tigris.subversion.SubversionException;

public interface Reporter {
  /**
   * Describe a working copy PATH as being at a particular REVISION;
   * this will *override* any previous set_path() calls made on parent
   * paths.  PATH is relative to the URL specified in open(), and must
   * be given in `svn_path_url_style'.
   */
  public void setPath(Object reportBaton, String path, Revision revision)
    throws SubversionException;

  /**
   * Describing a working copy PATH as missing.
   */
  public void deletePath(Object reportBaton, String path)
    throws SubversionException;

  /**
   * WC calls this when the state report is finished; any directories
   * or files not explicitly `set' above are assumed to be at the
   * baseline revision originally passed into do_update().
   */
  public void finishReport(Object reportBaton) throws SubversionException;

  /**
   * If an error occurs during a report, this routine should cause the
   * filesystem transaction to be aborted & cleaned up.
   */
  public void abortReport(Object reportBaton) throws SubversionException;
}
