#!/bin/sh

WWWDIR="`dirname \"$0\"`"

if [ "$1" = "all" ]; then
  WARNFILE=".validation-warnings.$$.tmp"
  for f in "$WWWDIR"/*.html; do
    if ! grep -F -q '<!DOCTYPE' "$f"; then
      RESULT='No <!DOCTYPE'
    else
      if "$WWWDIR/validate.sh" "$f" 2>"$WARNFILE"; then
        RESULT='\033[32mvalid\033[0m'
      else
        WARNLINES="`cat \"$WARNFILE\" | wc -l`"
        RESULT='\033[31;1mINVALID ('"$WARNLINES"')\033[0m'
      fi
      cat "$WARNFILE"
    fi
    echo -e "$f: $RESULT"
  done
  rm -f "$WARNFILE"
  exit 0
fi

# Much of this script is concerned with setting up a local cache of the
# DTD and files required by the DTD, so they are not re-downloaded every time.

ensure ()
{
  LOCALFILE="$WWWDIR/`basename \"$1\"`"
  test ! -f "$LOCALFILE" && wget -O "$LOCALFILE" "$1"
}

ensure "http://style.tigris.org/tigris_transitional.dtd"
ensure "http://www.w3.org/TR/xhtml1/DTD/xhtml-lat1.ent"
ensure "http://www.w3.org/TR/xhtml1/DTD/xhtml-symbol.ent"
ensure "http://www.w3.org/TR/xhtml1/DTD/xhtml-special.ent"

# Do not need these, but the URLs are here for uncommenting if you like
#ensure "http://www.w3.org/TR/xhtml1/DTD/xhtml1-frameset.dtd"
#ensure "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd"
#ensure "http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd"

# If you have ongmls installed, then you almost certainly have a SGML
# declaration for XML installed already, but there is no cross-platform way of
# knowing where.  So, instead, just download one, since we had to download
# other things anyway.
ensure "http://www.w3.org/TR/xhtml1/DTD/xhtml.soc"
ensure "http://www.w3.org/TR/xhtml1/DTD/xhtml1.dcl"

if test ! -f "$WWWDIR/catalog"; then
  echo '
OVERRIDE YES
PUBLIC "-//Tigris//DTD XHTML 1.0 Transitional//EN" "tigris_transitional.dtd"
' > "$WWWDIR/catalog"
fi

export SP_CHARSET_FIXED=YES
export SP_ENCODING=XML

export SGML_CATALOG_FILES="$WWWDIR/xhtml.soc"

onsgmls -wxml -wno-inclusion -ges "$1"
