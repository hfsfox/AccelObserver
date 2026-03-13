/*
 *
 * Single-header C 11 INI-style config parser
 *
 * Config file layout:
 * # single line comment
 * ; also comment
 *
 * [section]          ; define section
 *
 * key = value        ; spaces around = ignored, correct
 * key=value          ; without spaces also correct
 * key = "value val"  ; braces saved, spaces in is part of value
 * key =              ; empty value (empty string "")
 *
 * [another section]
 * ; keys without section define associated with section "" (global)
 *
 */

#ifndef __CONFPARSER_H__
#define __CONFPARSER_H__

#if !defined(_WIN32)
	#define _POSIX_C_SOURCE 200112L
	#define _DEFAULT_SOURCE  1
#endif

// types and functions for public interface
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h> // for FILE*

// private finctional headers
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#ifdef _WIN32
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#include <windows.h>
	#include <shlobj.h>
	#pragma comment(lib, "shell32.lib")
#else
	#include <unistd.h>
	#include <sys/stat.h>
	#include <pwd.h>
#endif




#ifdef __cplusplus
	extern "C" {
#endif

#ifndef CONF_MAX_ENTRIES
	#define CONF_MAX_ENTRIES 512 // max pair values per file
#endif
#ifndef CONF_MAX_SECTION
	#define CONF_MAX_SECTION   64   // max section name len in bytes
#endif
#ifndef CONF_MAX_KEY
	#define CONF_MAX_KEY       64   // max key name len in bytes
#endif
#ifndef CONF_MAX_VALUE
	#define CONF_MAX_VALUE    512   // max value len in bytes
#endif
#ifndef CONF_MAX_LINE
	#define CONF_MAX_LINE    1024   // max one string len in file in bytes
#endif

// single config entry truct
typedef struct
{
	char section[CONF_MAX_SECTION];		// section name, "" for global keys
	char key[CONF_MAX_KEY];				// key name (low reg, normalised)
	char value[CONF_MAX_VALUE];			// string value (without braces)
	int line_n;							// line number in file (not used, diagnostic purpose only)
} conf_entry_t;


// parsing result struct

typedef struct conf_result_t conf_result_t;

struct conf_result_t
{
	conf_entry_t* entries; // dynamic array of entries
	int filled_count; // count of filled entries
	int capacity; // allocated capacity
	char last_error_msg[256]; // last error message for debug, empty if no errors
	int error_line;
};

// private functional
//trim spaces around
inline static char*
str_trim(char* s)
{
	if (!s) return s;
	// left
	while (*s && isspace((unsigned char)*s)) ++s;
	if (*s == '\0') return s;
	// right
	char* end = s + strlen(s) - 1;
	while (end > s && isspace((unsigned char)*end)) --end;
	*(end + 1) = '\0';
	return s;
}

// make string in lowercase
inline static void
str_tolower(char* s)
{
	for (; *s; ++s) *s = (char)tolower((unsigned char)*s);
}

// remove braces
inline static char*
str_unquote(char* s)
{
	size_t len = strlen(s);
	if (len >= 2 && s[0] == '"' && s[len - 1] == '"')
	{
		s[len - 1] = '\0';
		return s + 1;
	}
	return s;
}

// check file existence
inline static bool
file_exists(const char* path)
{
	#ifdef _WIN32
		return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
	#else
		return access(path, F_OK) == 0;
	#endif
}

// create of struct
inline static
conf_result_t*
conf_alloc(void)
{
	conf_result_t* conf = (conf_result_t*)calloc(1, sizeof(conf_result_t));
	if (!conf) return NULL;
	conf->capacity = 64;
	conf->entries  = (conf_entry_t*)calloc((size_t)conf->capacity, sizeof(conf_entry_t));
	if (!conf->entries)
	{
		free(conf);
		return NULL;
	}
	return conf;
}

inline static bool
conf_push(conf_result_t* conf, const char* section,
					 const char* key, const char* value, int lineno)
{
	if (conf->count >= conf->capacity)
	{
		// delete buf if overflown
		if (conf->capacity >= CONF_MAX_ENTRIES) return false;
		int newcap = conf->capacity * 2;
		if (newcap > CONF_MAX_ENTRIES) newcap = CONF_MAX_ENTRIES;
		conf_entry_t* newbuf = (conf_entry_t*)realloc(
			conf->entries, (size_t)newcap * sizeof(conf_entry_t));
		if (!newbuf) return false;
		conf->entries  = newbuf;
		conf->capacity = newcap;
	}

	conf_entry_t* e = &conf->entries[conf->count++];
	strncpy(e->section, section, CONF_MAX_SECTION - 1);
	e->section[CONF_MAX_SECTION - 1] = '\0';

	strncpy(e->key, key, CONF_MAX_KEY - 1);
	e->key[CONF_MAX_KEY - 1] = '\0';
	str_tolower(e->key);       // keys case insensitive

	strncpy(e->value, value, CONF_MAX_VALUE - 1);
	e->value[CONF_MAX_VALUE - 1] = '\0';

	e->lineno = lineno;
	return true;
}

inline void
conf_free(conf_result_t* conf)
{
	if (!conf) return;
	free(conf->entries);
	free(conf);
}


// public functional


Iniconf* CONF_load_fp(FILE* fp, const char* source_name) {
	Iniconf* conf = conf_alloc();
	if (!conf) return NULL;

	char    cur_section[CONF_MAX_SECTION] = "";
	char    line[CONF_MAX_LINE];
	int     lineno = 0;

	while (fgets(line, sizeof(line), fp)) {
		++lineno;

		char* nl = strpbrk(line, "\r\n");
		if (nl) *nl = '\0';

		char* p = str_trim(line);

		if (*p == '\0') continue;

		if (*p == '#' || *p == ';') continue;

		if (*p == '[') {
			char* end = strchr(p, ']');
			if (!end) {
				snprintf(conf->error_msg, sizeof(conf->error_msg),
						 "%s:%d: unclosed '[' in section header",
			 source_name ? source_name : "?", lineno);
				conf->error_line = lineno;
				continue;
			}
			*end = '\0';
			char* sec = str_trim(p + 1);
			strncpy(cur_section, sec, CONF_MAX_SECTION - 1);
			cur_section[CONF_MAX_SECTION - 1] = '\0';
			continue;
		}

		char* eq = strchr(p, '=');
		if (!eq)
		{
			if (conf->error_line == 0) {
				snprintf(conf->error_msg, sizeof(conf->error_msg),
						 "%s:%d: line has no '=', ignored",
			 source_name ? source_name : "?", lineno);
				conf->error_line = lineno;
			}
			continue;
		}

		*eq = '\0';
		char* key_raw = str_trim(p);
		char* val_raw = str_trim(eq + 1);

		if (val_raw[0] != '"') {
			char* cmt = strpbrk(val_raw, "#;");
			if (cmt)
			{
				if (cmt > val_raw && isspace((unsigned char)*(cmt - 1)))
				{
					*cmt = '\0';
					val_raw = str_trim(val_raw);
				}
			}
		}

		val_raw = str_unquote(val_raw);

		if (key_raw[0] == '\0') continue;

			if (!conf_push(conf, cur_section, key_raw, val_raw, lineno)) {
				snprintf(conf->error_msg, sizeof(conf->error_msg),
						 "%s: reached maximum entry limit (%d)",
						 source_name ? source_name : "?", CONF_MAX_ENTRIES);
				break;
			}
	}

	return conf;
}

// load conf-style config from path
inline conf_result_t*
conf_load(const char* path, char* err_out)
{
	conf_result_t* conf_r = conf_alloc();
	if (!conf) return NULL; // check if created
	char    cur_section[CONF_MAX_SECTION] = "";
	char    line[CONF_MAX_LINE];
	int     lineno = 0;

	while (fgets(line, sizeof(line), fp))
	{
		++lineno;

		// remove newline
		char* nl = strpbrk(line, "\r\n");
		if (nl) *nl = '\0';

		char* p = str_trim(line);

		// empty string
		if (*p == '\0') continue;

		// detect comments
		if (*p == '#' || *p == ';') continue;

		// detect [section name]
		if (*p == '[')
		{
			char* end = strchr(p, ']');
			if (!end)
			{
				snprintf(conf->error_msg, sizeof(conf->error_msg),
						 "%s:%d: unclosed '[' in section header",
			 source_name ? source_name : "?", lineno);
				conf->error_line = lineno;
				continue;
			}
			*end = '\0';
			char* sec = str_trim(p + 1);
			strncpy(cur_section, sec, CONF_MAX_SECTION - 1);
			cur_section[CONF_MAX_SECTION - 1] = '\0';
			continue;
		}

		/* key = value */
		char* eq = strchr(p, '=');
		if (!eq)
		{
			/* string without '=' - warn and ignored */
			if (conf->error_line == 0)
			{
				snprintf(conf->error_msg, sizeof(conf->error_msg),
						 "%s:%d: line has no '=', ignored",
			 source_name ? source_name : "?", lineno);
				conf->error_line = lineno;
			}
			continue;
		}

		// separate keys and values
		*eq = '\0';
		char* key_raw = str_trim(p);
		char* val_raw = str_trim(eq + 1);

		// trim inline-comments from value (not in braces)
		if (val_raw[0] != '"')
		{
			char* cmt = strpbrk(val_raw, "#;");
			if (cmt)
			{
				// make sure before # space exist
				if (cmt > val_raw && isspace((unsigned char)*(cmt - 1)))
				{
					*cmt = '\0';
					val_raw = str_trim(val_raw);
				}
			}
		}

		val_raw = str_unquote(val_raw);

		if (key_raw[0] == '\0') continue; // skip empty keys

			if (!conf_push(conf, cur_section, key_raw, val_raw, lineno))
			{
				snprintf(conf->error_msg, sizeof(conf->error_msg),
						 "%s: reached maximum entry limit (%d)",
						 source_name ? source_name : "?", CONF_MAX_ENTRIES);
				break;
			}
	}

	return conf;
};

// entry search
inline static const conf_entry_t*
find_entry(const conf_result_t* conf, const char*   section, const char*   key)
{
	if (!conf || !key) return NULL;
	const char* sec = section ? section : "";

	// keys to lowercase
	char key_lc[CONF_MAX_KEY];
	strncpy(key_lc, key, CONF_MAX_KEY - 1);
	key_lc[CONF_MAX_KEY - 1] = '\0';
	str_tolower(key_lc);

	for (int i = 0; i < conf->count; ++i)
	{
		const conf_entry_t* e = &conf->entries[i];
		if (strcasecmp(e->section, sec) == 0 &&
			strcmp(e->key, key_lc) == 0)
		{
			return e;
		}
	}
	return NULL;
}


bool conf_has_key(const conf_result_t* conf, const char* section, const char* key)
{
	return find_entry(conf, section, key) != NULL;
}

int conf_count(const conf_result_t* conf)
{
	return conf ? conf->count : 0;
}

const char* conf_error(const conf_result_t* conf)
{
	return conf ? conf->error_msg : "";
}

const char* conf_get_str(const conf_result_t* conf,
						const char*   section,
						const char*   key,
						const char*   fallback)
{
	const conf_entry_t* e = find_entry(conf, section, key);
	return e ? e->value : fallback;
}

int conf_get_int(const conf_result_t* conf,
				const char*   section,
				const char*   key,
				int           fallback)
{
	const conf_entry_t* e = find_entry(conf, section, key);
	if (!e || e->value[0] == '\0') return fallback;
	char* end;
	long v = strtol(e->value, &end, 0);
	return (end != e->value) ? (int)v : fallback;
}

long conf_get_long(const conf_result_t* conf,
				  const char*   section,
				  const char*   key,
				  long          fallback)
{
	const conf_entry_t* e = find_entry(conf, section, key);
	if (!e || e->value[0] == '\0') return fallback;
	char* end;
	long v = strtol(e->value, &end, 0);
	return (end != e->value) ? v : fallback;
}

double conf_get_double(const conf_result_t* conf,
					  const char*   section,
					  const char*   key,
					  double        fallback)
{
	const conf_entry_t* e = find_entry(conf, section, key);
	if (!e || e->value[0] == '\0') return fallback;
	char* end;
	double v = strtod(e->value, &end);
	return (end != e->value) ? v : fallback;
}

bool conf_get_bool(const conf_result_t* conf,
				  const char*   section,
				  const char*   key,
				  bool          fallback)
{
	const conf_entry_t* e = find_entry(conf, section, key);
	if (!e || e->value[0] == '\0') return fallback;

	// normalize to lowercase for compare
	char v[16];
	strncpy(v, e->value, sizeof(v) - 1);
	v[sizeof(v) - 1] = '\0';
	str_tolower(v);

	if (strcmp(v, "true")  == 0 || strcmp(v, "yes") == 0 ||
		strcmp(v, "on")    == 0 || strcmp(v, "1")   == 0) return true;
	if (strcmp(v, "false") == 0 || strcmp(v, "no")  == 0 ||
		strcmp(v, "off")   == 0 || strcmp(v, "0")   == 0) return false;
	return fallback;
}


uint16_t conf_get_uint16(const conf_result_t* conf,
						const char*   section,
						const char*   key,
						uint16_t      fallback)
{
	int v = conf_get_int(conf, section, key, (int)fallback);
	if (v < 0 || v > 65535) return fallback;
	return (uint16_t)v;
}

uint32_t conf_get_uint32(const conf_result_t* conf,
						const char*   section,
						const char*   key,
						uint32_t      fallback)
{
	const conf_entry_t* e = find_entry(conf, section, key);
	if (!e || e->value[0] == '\0') return fallback;
	char* end;
	unsigned long v = strtoul(e->value, &end, 0);
	return (end != e->value) ? (uint32_t)v : fallback;
}

size_t conf_get_size(const conf_result_t* conf,
					const char*   section,
					const char*   key,
					size_t        fallback)
{
	const conf_entry_t* e = find_entry(conf, section, key);
	if (!e || e->value[0] == '\0') return fallback;
	char* end;
	unsigned long long v = strtoull(e->value, &end, 0);
	return (end != e->value) ? (size_t)v : fallback;
}


/* --------------------------------------------------------------------------- */
const conf_entry_t* conf_get_entry(const conf_result_t* conf, int index) {
	if (!conf || index < 0 || index >= conf->count) return NULL;
	return &conf->entries[index];
}

void conf_dump(const conf_result_t* conf, FILE* fp) {
	if (!conf || !fp) return;
	fprintf(fp, "# conf_result_t dump: %d entries\n", conf->count);
	const char* prev_sec = NULL;
	for (int i = 0; i < conf->count; ++i) {
		const conf_entry_t* e = &conf->entries[i];
		if (prev_sec == NULL || strcmp(prev_sec, e->section) != 0) {
			fprintf(fp, "\n[%s]\n", e->section[0] ? e->section : "(global)");
			prev_sec = e->section;
		}
		fprintf(fp, "  %-30s = %s\n", e->key, e->value);
	}
}

/* --------------------------------------------------------------------------- */
/* Поиск файла конфигурации по стандартным путям                             */
/* --------------------------------------------------------------------------- */
bool conf_find_config(const char* app_name,
					 const char* explicit_path,
					 char*       out_path,
					 size_t      out_size)
{
	// 1. Explicitly defined (from --config)
	if (explicit_path && explicit_path[0] != '\0') {
		if (file_exists(explicit_path)) {
			strncpy(out_path, explicit_path, out_size - 1);
			out_path[out_size - 1] = '\0';
			return true;
		}
		// if not found give error to main
		strncpy(out_path, explicit_path, out_size - 1);
		out_path[out_size - 1] = '\0';
		return false;
	}

	char candidate[512];

	// 2. corrent dir: ./app_name.conf
	snprintf(candidate, sizeof(candidate), "./%s.conf", app_name);
	if (file_exists(candidate)) {
		strncpy(out_path, candidate, out_size - 1);
		out_path[out_size - 1] = '\0';
		return true;
	}

	#ifdef _WIN32
	//3. Windows: %APPDATA%\app_name\app_name.conf
	char appdata[MAX_PATH];
	if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata))) {
		snprintf(candidate, sizeof(candidate), "%s\\%s\\%s.conf",
				 appdata, app_name, app_name);
		if (file_exists(candidate)) {
			strncpy(out_path, candidate, out_size - 1);
			out_path[out_size - 1] = '\0';
			return true;
		}
	}
	#else
	// 4. Unix: ~/.config/app_name/app_name.conf
	const char* home = getenv("HOME");
	if (!home) {
		struct passwd* pw = getpwuid(getuid());
		if (pw) home = pw->pw_dir;
	}
	if (home) {
		snprintf(candidate, sizeof(candidate), "%s/.config/%s/%s.conf",
				 home, app_name, app_name);
		if (file_exists(candidate)) {
			strncpy(out_path, candidate, out_size - 1);
			out_path[out_size - 1] = '\0';
			return true;
		}
	}

	// 5. Unix: /etc/app_name/app_name.conf
	snprintf(candidate, sizeof(candidate), "/etc/%s/%s.conf",
			 app_name, app_name);
	if (file_exists(candidate)) {
		strncpy(out_path, candidate, out_size - 1);
		out_path[out_size - 1] = '\0';
		return true;
	}
	#endif

	out_path[0] = '\0';
	return false;
}





#ifdef __cplusplus
	}
#endif

#endif
