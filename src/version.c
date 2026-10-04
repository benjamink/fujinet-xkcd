/* src/version.c - XKCD_VERSION comes from the top-level Makefile (the release tag) */
#ifndef XKCD_VERSION
#define XKCD_VERSION "0.1"
#endif
const char xkcd_version[] = "$VER: xkcd " XKCD_VERSION " (" GIT_VERSION ")";
