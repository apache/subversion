/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2004 CollabNet.  All rights reserved.
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
 */
package org.tigris.subversion.javahl;
/**
 * this is the interface for requesting authentification information from the
 * user. Should the javahl bindings need the matching information, these
 * methodes will be called.
 */
public interface PromptUserPassword3 extends PromptUserPassword2
{
    /**
     * Request the password to be used from the user.
     * the save data check box status will be queried by userAllowedSave
     * @param realm     realm for the username
     * @param username  username in the realm
     * @param maySave   should a save data check box be enabled.
     * @return          password as entered or null if canceled.
     */
    public boolean prompt(String realm, String username, boolean maySave);
    /**
     *  Ask the user a question about authentification
     * the save data check box status will be queried by userAllowedSave
     * @param realm         real of the question
     * @param question      text of the question
     * @param showAnswer    flag if the answer should be displayed
     * @param maySave       should a save data check box be enabled.
     * @return              answer as entered or null if canceled
     */
    public String askQuestion(String realm, String question, boolean showAnswer, boolean maySave);
    /**
     * query if the user allowed the saving of the data of the last call
     * @return      was the save data check box checked
     */
    public boolean userAllowedSave();
}
