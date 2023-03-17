#!/bin/bash

checkRodata(){
  str=$(objdump -s -j .rodata "$@" | sed 's/^.*  //g' | tr --delete '\n' | sed 's/\./\n/g' | grep '^_cython_0' | head -n 1 | sed 's/_cython_//g')
  if [ ! -z "$str" ]; then
    if echo "$str" | egrep --quiet '(29_29|30|31|32|33|34|35|36|37|38|39|40)'; then
      if [ "true" == "${SHOW_GOOD:-}" ]; then
        echo -e "<\e[1mrodata pass\e[0m>[\e[1;32m$str\e[0m] ""$@"
      fi
    else
      echo -e "<\e[1mrodata fail\e[0m>[\e[1;31m$str\e[0m] ""$@"
    fi
  fi
}

checkFastCall(){
  str=$(readelf -sW "$@" | grep '__Pyx_PyCFunction_FastCall' | wc -l)
  if [ "0" == "$str" ]; then
      if [ "true" == "${SHOW_GOOD:-}" ]; then
        echo -e "<\e[1msymtab pass\e[0m>[\e[1;32m$str\e[0m] ""$@"
      fi
  else
      echo -e "<\e[1msymtab fail\e[0m>[\e[1;31m$str\e[0m] ""$@"
  fi
}

checkCystuff(){
  checkRodata "$@"
  checkFastCall "$@"
}

export -f checkRodata
export -f checkFastCall
export -f checkCystuff
export SHOW_GOOD=false
find . -name "*.so" -type f -exec bash -c 'checkCystuff "{}"' \;
