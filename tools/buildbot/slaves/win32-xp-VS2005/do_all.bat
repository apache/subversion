cmd.exe /c ..\svnbuild.bat > build.log
cmd.exe /c ..\svncheck.bat fsfs ra_local > fsfs_local.log
cmd.exe /c ..\svncheck.bat fsfs ra_svn > fsfs_svn.log
cmd.exe /c ..\svncheck.bat fsfs ra_dav > fsfs_dav.log
cmd.exe /c ..\svncheck.bat bdb ra_local > bdb_local.log
cmd.exe /c ..\svncheck.bat bdb ra_svn > bdb_svn.log
cmd.exe /c ..\svncheck.bat bdb ra_dav > bdb_dav.log
