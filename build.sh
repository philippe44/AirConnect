#!/bin/bash

list="x86_64-linux-gnu-gcc x86-linux-gnu-gcc arm-linux-gnueabi-gcc aarch64-linux-gnu-gcc sparc64-linux-gnu-gcc mips-linux-gnu-gcc powerpc-linux-gnu-gcc x86_64-macos-darwin-gcc x86_64-freebsd-gnu-gcc x86_64-solaris-gnu-gcc"
declare -A alias=( [x86-linux-gnu-gcc]=i686-linux-gnu-gcc [x86_64-macos-darwin-gcc]=x86_64-apple-darwin19-gcc [x86_64-freebsd-gnu-gcc]=x86_64-gnu-freebsd13.1-gcc [x86_64-solaris-gnu-gcc]=x86_64-gnu-solaris2.x-gcc )
declare -A cppflags=( [mips-linux-gnu-gcc]="-march=mips32" [powerpc-linux-gnu-gcc]="-m32")
declare -a compilers

IFS= read -ra candidates <<< "$list"

# do we have "clean" somewhere in parameters (assuming no compiler has "clean" in it...
if [[ $@[*]} =~ clean ]]; then
	clean="clean"
fi	

# first select platforms/compilers
for cc in ${candidates[@]}
do
	# check compiler first
	if ! command -v ${alias[$cc]:-$cc} &> /dev/null; then
		continue
	fi
	
	if [[ $# == 0 || ($# == 1 && -n $clean) ]]; then
		compilers+=($cc)
		continue
	fi

	for arg in $@
	do
		if [[ $cc =~ $arg ]]; then 
			compilers+=($cc)
		fi
	done
done

# then do the work
for cc in ${compilers[@]}
do
	IFS=- read -r platform host dummy <<< $cc

	make CC=${alias[$cc]:-$cc} HOST=$host PLATFORM=$platform $clean
	if [[ -n $clean ]]; then
		continue
	fi
done
