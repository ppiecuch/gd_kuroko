#!/bin/bash

set -e

KUROKO='1.4.0'

trap "{ if [ -d "kuroko-${KUROKO}" ]; then rm -rf "kuroko-${KUROKO}"; fi; exit 255; }" SIGINT SIGTERM ERR EXIT

rm -rf kuroko-${KUROKO} modules src tools demo

curl -L https://github.com/kuroko-lang/kuroko/archive/refs/tags/v${KUROKO}.tar.gz | tar -xzf -

mv kuroko-${KUROKO}/modules .
mv kuroko-${KUROKO}/src .
mkdir -p tools demo
mv kuroko-${KUROKO}/tools/compile.c kuroko-${KUROKO}/tools/sandbox.c kuroko-${KUROKO}/tools/simple-repl.h tools/
mv kuroko_main.c demo/

rm -rf kuroko-${KUROKO}

echo "** DONE **"
