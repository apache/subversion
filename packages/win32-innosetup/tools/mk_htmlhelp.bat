@ECHO OFF
SET BOOKNAME=book
cd %BOOKNAME%
SET XLSPARAMS=--param suppress.navigation 0 --param htmlhelp.hhc.binary 1 --param htmlhelp.show.advanced.search 1
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.chm '%BOOKNAME%.chm' --param htmlhelp.hhp '%BOOKNAME%.hhp'
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.hhk '%BOOKNAME%.hhk' --param htmlhelp.hhc '%BOOKNAME%.hhc'
SET XLSPARAMS=%XLSPARAMS% --param html.stylesheet.type 'text/css' --param html.stylesheet '../svn-doc.css'
xsltproc %XLSPARAMS% ..\tools\xsl\htmlhelp\htmlhelp.xsl %BOOKNAME%.xml

SET FOO=name=\"Local\" value=\"
SET BAR=name=\"Local\" value=\"%BOOKNAME%\/
perl  -pi.bak -e "s/%FOO%/%BAR%/g" %BOOKNAME%.hhc
cd ..

SET BOOKNAME=misc-docs
cd %BOOKNAME%
SET XLSPARAMS=--param suppress.navigation 0 --param htmlhelp.hhc.binary 1 --param htmlhelp.show.advanced.search 1
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.chm '%BOOKNAME%.chm' --param htmlhelp.hhp '%BOOKNAME%.hhp'
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.hhk '%BOOKNAME%.hhk' --param htmlhelp.hhc '%BOOKNAME%.hhc'
SET XLSPARAMS=%XLSPARAMS% --param html.stylesheet.type 'text/css' --param html.stylesheet '../svn-doc.css'
xsltproc %XLSPARAMS% ..\tools\xsl\htmlhelp\htmlhelp.xsl %BOOKNAME%.xml

SET FOO=name=\"Local\" value=\"
SET BAR=name=\"Local\" value=\"%BOOKNAME%\/
perl  -pi.bak -e "s/%FOO%/%BAR%/g" %BOOKNAME%.hhc
cd..
