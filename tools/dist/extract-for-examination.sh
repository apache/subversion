#!/bin/sh

for i in *.tar.bz2; do
  base=${i%.tar.bz2}-tar-bz2
  mkdir $base
  cd $base
  tar -jxvf ../$i
  cd ..
done

for i in *.zip; do
  base=${i%.zip}-zip
  mkdir $base
  cd $base
  unzip ../$i
  cd ..
done
