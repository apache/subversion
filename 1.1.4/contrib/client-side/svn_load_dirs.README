Introduction
============

This Perl script is designed to load a number of directories into
Subversion.  This is useful if you have a number of .zip's or
tar.{Z,gz,bz2}'s for a particular package and want to load them into
Subversion.

Command Line Options
====================

Run the script with no command line arguments to see all the command
line options it takes.

When Not To Use This Script
===========================

This script assumes that these packages were not previously in a
source control system, in particular CVS, because then you would use
another script to migrate the repository over, and in CVS' case, you
would use cvs2svn.  This script will properly tag each release in the
tags directory if you use the -t command line option.

Automatically Setting Properties On New Files & Directories
===========================================================

The script also accepts a separate configuration file for applying
properties to specific files and directories matching a regular
expression that are *added* to the repository.  This script will not
modify properties of already existing files or directories in the
repository.  This configuration file is specified to svn_load_dirs.pl
using the -p command line option.  The format of the file is either
two or four columns:

regular_expression  control  property_name  property_value

   The `regular_expression' is a Perl style regular expression.  It is
   matched in a case-insensitive against filenames.

   The `control' must either be set to `break' or `cont'.  It is used
   to tell svn_load_dirs.pl if the following lines in the
   configuration file should be examined for a match or if all
   matching should stop.  If `control' is set to `break', then no more
   lines from the configuration file will be matched.  If `control' is
   set to `cont', which is short for continue, then more comparisons
   will be made.  Multiple properties can be set for one file or
   directory this way.

   The last two, `property_name' and `property_value' are optional and
   are applied to matching files and directories.

If you have whitespace in any of the `regular_expression',
`property_name' or `property_value' columns, you must surround the
value with either a single or double quote.  You can protect single or
double quotes with a \ character.  The \ character is removed by this
script *only* for whitespace and quote characters, so you do not need
to protect any other characters, beyond what you would normally
protect for the regular expression.

This sample configuration file was used to load on a Unix box a number
of Zip files containing Windows files with CRLF end of lines.

   \.doc$              break   svn:mime-type   application/msword
   \.ds(p|w)$          break   svn:eol-style   CRLF
   \.ilk$              break   svn:eol-style   CRLF
   \.ncb$              break   svn:eol-style   CRLF
   \.opt$              break   svn:eol-style   CRLF
   \.exe$              break   svn:mime-type   application/octet-stream
   dos2unix-eol\.sh$   break
   .*                  break   svn:eol-style   native

In this example, all the files should be converted to the native end
of line style, which the last line of the configuration handles.  The
exception is dos2unix-eol.sh, which contains embedded CR's used to
find and replace Windows CRLF end of line characters with Unix's LF
characters.  Since svn and svn_load_dirs.pl converts all CR, CRLF and
LF's to the native end of line style when `svn:eol-style' is set to
`native', this file should be left untouched.  Hence, the `break' with
no property settings.

The Windows Visual C++ and Visual Studio files (*.dsp, *.dsw, etc.) 
should retain their CRLF line endings on any operating system and any
*.doc files are always treated as binary files, hence the
`svn:mime-type' setting of `application/msword'.

Example Import
==============

An example import follows:

Steps:

1) Unpack your .tar.{Z,gz,bz2}'s or .zips into a directory that is not
   in a Subversion repository.

   Example:

   I'll use an example from my Orca distribution:

      % cd /tmp
      % zcat orca-0.18.tar.gz | tar xf -
      % zcat orca-0.27b2.tar.gz | tar xf -

2) Decide on the directory structure you want to use to contain the
   project you are loading.

   There are three main directory structures you can use.  If you have
   a single project, then use the structure Subversion uses for
   itself, that is

      /branches
      /tags
      /trunk

   and load the project into /trunk and the tags into the tags
   directory.

   If you have more than one project and you want to treat each
   project separately, then use one of the following structures:

      /branches
      /tags
      /tags/project1
      /tags/project2
      /tags/project3
      /trunk
      /trunk/project1
      /trunk/project2
      /trunk/project3

   or

      /project1/branches
      /project1/tags
      /project1/trunk
      /project2/branches
      /project2/tags
      /project2/trunk

   Example:

   To load Orca using the second directory structure into the
   subversion repository rooted at http://svn.orcaware.com:8000/repos

      % cd /tmp
      % svn co http://svn.orcaware.com:8000/repos
      % cd repos
      % mkdir tags tags/orca trunk trunk/orca
      % svn add tags trunk
      % svn commit -m 'Create initial directory tree structure for projects.'

   This script will create any subdirectories required to import your
   directories into the repository, so these steps may not be required.

3) Decide on the URL to use to access the subversion repository with
   this script and the relative directory paths to the directories to
   import your project into and to place the tags into.

   The usage of the script is

   ./svn_load_dirs.pl [-t tag_dir] svn_url import_dir dir_v1 [dir_v2 [..]]

   The import_dir and tag_dir command line options are directory paths
   relative to svn_url and tell the script where to load your project
   and optionally the tags.  Both import_dir and tag_dir cannot
   contain any ..'s and so svn_url must contain both import_dir and
   tag_dir.

   This script supports importing your directories into subdirectories
   of the root of the subversion repository.

   Example:

   In the previous step, if you wanted to load a project named orca
   into the second directory structure, say

      /orca/branches
      /orca/tags
      /orca/trunk

   and you didn't care about tags, then you could use as svn_url the
   URL

      http://svn.orcaware.com:8000/repos/orca

   and use . as import_dir.

   In this case, the script will only check out the orca subdirectory.
   This is handy if the entire repository is very large and you don't
   want this script to check the whole repository under /repos out to
   load files into it.

   The only caveat is that svn_url must exist already in the
   repository.  So in this case, you would have to already have
   created the orca subdirectory in the repository.

4) Decide on the tags you want on your directories.  If you don't want
   any tags, then ignore this step.

   The script takes a -t command line argument that is a directory
   path relative to the svn_url that you supply to this script from
   step 3 above.  Again, the URL from step 3 does not need to be the
   URL of the root of the subversion repository, so you can work in
   the subdirectory just fine.

   Look at the directories that will be loaded into the repository and
   come up with a Perl regular expression that matches only the
   portion of the directory name that identifies each directory.  You
   may need to rename your directories so that they contain a version
   number you can use to tag them properly.

   The regular expression should be placed into the directory path
   given to -t surrounded by @'s.  Make sure to protect the regular
   expression from the shell by using quotes.

   You can have multiple sets of regular expressions in the directory
   path.

   There is no way to escape the @ characters.

   Example:

   For the Orca directories orca-0.18 and orca-0.27b2 I can use the
   regular expression \d+\.\w+.  I want the tags to be located in
   the tags/orca/VERSION_NUMBER directory.  So I would use:

      -t 'tags/orca/@\d+\.\w+@'

5) Back up your Subversion repository in case you are not happy with
   the results of running the script or the script fails for some
   reason.

   Example:

   % /opt/i386-linux/apache-2.0/bin/apachectl stop
   % cd /export/svn
   % tar cvf repos_backup.tar repos
   % /opt/i386-linux/apache-2.0/bin/apachectl start

6) Run this script.  The first argument is the root of the Subversion
   package directory where you want to install the directories.  The
   directories are loaded in order that they appear on the command
   line.

   Example:

      svn_load_dirs.pl http://svn.orcaware.com:8000/repos \
         trunk/orca -t 'tags/orca/@\d+\.\w+@' orca-0.18 orca-0.27b2

   The output from this script are:

      A Added file or directory.
      U File's contents have been updated.
      d File or directory is deleted because an enclosing directory is
        deleted.
      D File or directory is deleted.

7) Check the results by either checking out a new tree and or browsing
   the repository with a browser.  If they are not what you want, back
   out the changes.

   Example:

   These commands back out the changes:

   % /opt/i386-linux/apache-2.0/bin/apachectl stop
   % cd /export/svn
   % rm -fr repos
   % tar xvf repos_backup.tar
   % /opt/i386-linux/apache-2.0/bin/apachectl start
