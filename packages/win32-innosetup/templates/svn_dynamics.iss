; This is where you can let Inno Setup know where to find the files included in
; the setup.
; The paths can be relative to the "packages\win32-innosetup" path in the
; Subversion repository or they can be absolute (full path).
; This file must reside in the "packages\win32-innosetup" directory so copy it
; to there and edit it.
; This file is ignored by the Subversion repository when it resides in
; "packages\win32-innosetup". That's because the many people who want to make
; distros for Subversion have different preferences (compilers, sources, etc.).
;
; #############################################################################
; Edit this variables and make sure that all files are where they should be
; according to this file
;
; Misc files such as svn.chm goes here:
#define path_setup_in "in"
; This is where the final setup ends
#define path_setup_out "out"
; Path to Inno Setup Executable
#define path_is "E:\Inno Setup4"

;Build defines ----------------------------------------------------------------
; File names
#define libdb_dll "libdb43.dll"

; Uncomment the defines if true
#define inc_dbgsyms
#define VC7
#define inc_locale

; Apache Server: The minimum required version
#define apache_ver_min "2.0.49"

; You should edit the paths below the examples according to your system
#define path_svn_win32 "C:\work\Subversion\binaries_svn\svn-win32\"

;Subversion Core --------------------------------------------------------------
; Basic files
#define path_svnclient         (path_svn_win32 + "bin")
#define path_svnadmin          (path_svn_win32 + "bin")
#define path_svnlook           (path_svn_win32 + "bin")
#define path_svnserve          (path_svn_win32 + "bin")
#define path_svnversion        (path_svn_win32 + "bin")
#define path_svndumpfilter     (path_svn_win32 + "bin")
#define path_locale            (path_svn_win32 + "share\locale")
; APR
#define path_libapr_dll        (path_svn_win32 + "bin")
#define path_libaprutil_dll    (path_svn_win32 + "bin")
#define path_libapriconv_dll   (path_svn_win32 + "bin")
#define path_iconv_dll         (path_svn_win32 + "bin")
#define path_intl_dll          (path_svn_win32 + "bin")
; httpd
#define path_davsvn            (path_svn_win32 + "bin")
#define path_authzsvn          (path_svn_win32 + "bin")
; Misc
#define path_msvcr70_dll       (path_svn_win32 + "bin")
#define path_brkdb_dll         (path_svn_win32 + "bin")
#define path_svnpath           "tools\svnpath"
#define path_iconv             (path_svn_win32 + "iconv")
#define path_ssl               (path_svn_win32 + "bin")

;Debug symbols ----------------------------------------------------------------
; Basic files
#define path_svnclient_pdb     (path_svn_win32 + "bin")
#define path_svnadmin_pdb      (path_svn_win32 + "bin")
#define path_svnlook_pdb       (path_svn_win32 + "bin")
#define path_svnserve_pdb      (path_svn_win32 + "bin")
#define path_svnversion_pdb    (path_svn_win32 + "bin")
#define path_svndumpfilter_pdb (path_svn_win32 + "bin")
; httpd
#define path_davsvn_pdb        (path_svn_win32 + "bin")
#define path_authzsvn_pdb      (path_svn_win32 + "bin")
; Misc
#define path_iconv_pdb         (path_svn_win32 + "iconv")
#define path_libapr_pdb        (path_svn_win32 + "lib\apr")
#define path_libapu_pdb        (path_svn_win32 + "lib\apr-util")
#define path_py_libsvn_pdb     (path_svn_win32 + "python\libsvn")

;Development -------------------------------------------------------------------
#define path_dev_doc           (path_svn_win32 + "doc")
#define path_dev_inc           (path_svn_win32 + "include")
#define path_dev_inc_apr       (path_svn_win32 + "include\apr")
#define path_dev_inc_apu       (path_svn_win32 + "include\apr-util")
#define path_dev_lib           (path_svn_win32 + "lib")
#define path_dev_lib_apr       (path_svn_win32 + "lib\apr")
#define path_dev_lib_apu       (path_svn_win32 + "lib\apr-util")

;Python bindings
#define path_py_bind_libsvn    (path_svn_win32 + "python\libsvn")
#define path_py_bind_svn       (path_svn_win32 + "python\svn")
