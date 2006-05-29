#!/bin/sh
COMPONENT_XML_FILES="goals.xml model.xml architecture.xml deltas.xml "\
"client.xml protocol.xml server.xml license.xml"
perl -i.bak1~ -pe 's/&/¬/g;' $COMPONENT_XML_FILES
xsltproc --output design.html to-html.xsl design.xml
perl -i.bak2~ -pe 's/¬/&/g;' $COMPONENT_XML_FILES design.html
rm -f *.bak[12]~
