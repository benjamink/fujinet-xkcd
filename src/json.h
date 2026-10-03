/* src/json.h */
#ifndef XKCD_JSON_H
#define XKCD_JSON_H
int json_get_string(const char *buf, const char *end, const char *key, char *out, unsigned short max);
int json_get_long(const char *buf, const char *end, const char *key, long *out);
#endif
