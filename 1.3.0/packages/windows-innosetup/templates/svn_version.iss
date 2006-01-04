; This is where the Inno Setup compiler know where to find the version info for
; a new version of the distribution.
; Copy this file to the same place as svn.iss and edit it's content according
; to the version of svn you want to distribute. Another alternative is to run
; mk_distro and set the version info from there.
; This file is ignored by the Subversion repository when it resides in
; "packages\win32-innosetup".
#define svn_version "X.X.X"
#define svn_pretxtrevision "-r"
#define svn_revision "11581"
