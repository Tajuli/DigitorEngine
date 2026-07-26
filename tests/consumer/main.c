#include <digitor/digitor.h>
#include <stdio.h>
int main(void) { const char *version = digitor_get_version(); if (!version || !version[0]) return 1; puts(version); return 0; }
