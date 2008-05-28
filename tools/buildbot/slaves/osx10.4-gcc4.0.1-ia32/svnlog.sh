#!/bin/bash

# upload file to server
FILENAME=tests-`date +%Y%m%d%H%M`.log.tgz
tar -czf $FILENAME tests.log
ftp -n www.mobsol.be < ../ftpscript 
rm $FILENAME

echo "Logs of the testrun can be found here: http://www.mobsol.be/logs/osx10.4-gcc4.0.1-ia32/$FILENAME"

exit 0
