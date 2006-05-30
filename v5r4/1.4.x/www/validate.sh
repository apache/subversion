#!/bin/bash -e

WWWDIR="`dirname \"$0\"`"
ensure ()
{
  LOCALFILE="$WWWDIR/$2/`basename \"$1\"`"
  test -f "$LOCALFILE" || wget -O "$LOCALFILE" "$1"
}

# Download files necessary to preview the web pages locally.
if [ ! -r "$WWWDIR/tigris-branding/.download-complete" ]; then
  BRANDING_URL="http://subversion.tigris.org/branding"
  mkdir -p "$WWWDIR/tigris-branding/"{css,scripts,images}
  for i in tigris inst print; do
    ensure "$BRANDING_URL/css/$i.css" "tigris-branding/css"
  done
  ensure "$BRANDING_URL/scripts/tigris.js" "tigris-branding/scripts"
  for f in `sed -n -e 's,.*url(\.\./images/\([^)]*\).*,\1,;tp' \
    -etp -ed -e:p -ep $WWWDIR/tigris-branding/css/*.css`; do
    case $f in
      collapsed_big.gif|expanded_big.gif) ;; # 404!
      *) ensure "$BRANDING_URL/images/$f" "tigris-branding/images" ;;
    esac
  done
  touch "$WWWDIR/tigris-branding/.download-complete"
fi

# Check we have DTDs available
LOCAL_CATALOG="$WWWDIR/xhtml1.catalog"
if [ ! -r "$LOCAL_CATALOG" ]; then
  RESULT=`echo 'resolve "-//W3C//DTD XHTML 1.0 Strict//EN" ' \
  '"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd"' | xmlcatalog --shell`
  case $RESULT in
    */xhtml1-strict.dtd*) ;;
    *)
    ensure "http://www.w3.org/TR/xhtml1/xhtml1.tgz" "."
    rm -rf "$WWWDIR/xhtml1-20020801" "$LOCAL_CATALOG"
    tar -zxvf "$WWWDIR/xhtml1.tgz"
    xmlcatalog --noout --create "$LOCAL_CATALOG"
    xmlcatalog --noout --add rewriteSystem "http://www.w3.org/TR/xhtml1/" \
    "`cd \"$WWWDIR\" && pwd`/xhtml1-20020801/" "$LOCAL_CATALOG"
    ;;
  esac
fi
test -r "$LOCAL_CATALOG" && export XML_CATALOG_FILES="$LOCAL_CATALOG"

if [ $# -eq 0 ]; then echo "Usage: ./validate.sh <filename>..." >&2; exit 1; fi
if [ "$1" = "all" ]; then
  set - "$WWWDIR"/*.html "$WWWDIR"/merge-tracking/*.html
fi
if [ $# -eq 1 ]; then xmllint --nonet --noout --valid "$1"; exit $?; fi

for f in "$@"; do
  case $f in
    *project_tools.html) echo "$f: Skipped" ;;
    *.html) xmllint --nonet --noout --valid "$f" && echo -e \
    "$f: "'\033[32mvalid\033[0m' || echo -e "$f: "'\033[31;1mINVALID\033[0m' ;;
    *) echo "$f: Not HTML" ;;
  esac
done
