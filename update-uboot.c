/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Karamov Artur <karamov@fastwel.ru>
 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/types.h>
#include <argp.h>
#include <sys/stat.h>
#include <dirent.h>
#include <zlib.h>

//parser
#include "update-uboot.lex.h"
#include "update-uboot.bison.h"
#include "update-uboot.h"

#define CRC_SIZE sizeof(uint32_t)
#define MAX_DATA_LENGTH 256

//#define TEST
#if defined(TEST)
# define CFG_FILE	"example.cfg"
# define BACKUP_FILE	"env.back"
# define TMP_FILE	"uboot.current"
# define FLASH		"cpc313-mips.rom"
# define OF_FLASH_DEVICE	"./test/"
#else
# define CFG_FILE	"/boot/uboot.conf"
# define BACKUP_FILE	"/etc/default/uboot"
# define TMP_FILE	"/tmp/uboot.current"
# define FLASH		"/dev/mtdblock0"
# define OF_FLASH_DEVICE	"/sys/firmware/devicetree/base/apb/spi@1F040100/flash@0/"
#endif

#define SEC_UBOOT	"BOOTLOADER"
#define SEC_ENV		"ENVSET"
#define SEC_ROM		"ROM"

/**
 * @def DO
 * If expr is true, then print error and exit
 */
#define DO(expr, errcode, massage) \
	({ if (expr) {						\
		if (errcode)					\
			errno = errcode;			\
		if (massage)					\
			perror(massage);			\
		return EXIT_FAILURE;				\
	   };							\
	})

static int get_section(cur_task *a);

#define str(x) #x
#define to_str(x) str(x)

const char *argp_program_version = to_str(PROGNAME) " ver. is " to_str(VERSION);
//const char *argp_program_bug_address = ;

static char args_doc[] = " ";
static char doc[] = "Read/write partition in u-boot ROM";

static struct argp_option opt[] = {
	{"config", 'c', "FILE", 0, 			"Specify config file manually"},
	{"get",    'g', "FILE", OPTION_ARG_OPTIONAL, 	"Get current config. Saved in " TMP_FILE " by default. If file path set next to this parameter, then config saved in that file"},
	{"restore",'r',      0, 0, 			"Write default configs in ENVSET. " BACKUP_FILE " is used for default configs"},
	{"uboot",  'u', "FILE", 0, 			"Update uboot"},
	{"rom",    'm', "FILE", 0, 			"Write whole rom"},
	{0,	   'h',	     0, OPTION_HIDDEN, 		0},
	{0}
};

static error_t parse_opt(int key, char *arg, struct argp_state *st)
{
	cur_task *a = st->input;

	switch (key) {
		case 'c':
			a->cfg_file = arg;
			break;
		case 'g':
			a->cfg_file = NULL;
			if (arg == NULL && st->argv[st->next]) {
				arg = st->argv[st->next];
				st->next++;
			};
			a->tmp_file = arg ? arg : TMP_FILE;
			a->o = READ_ONLY;
			break;
		case 'r':
			a->cfg_file = BACKUP_FILE;
			a->tmp_file = NULL;
			a->o = WRITE_ONLY;
			break;
		case 'u':
			a->cfg_file = arg;
			a->tmp_file = NULL;
			a->sn = SEC_UBOOT;
			a->o = WRITE_ONLY;
			break;
		case 'm':
			a->cfg_file = arg;
			a->tmp_file = NULL;
			a->sn = SEC_ROM;
			a->o = WRITE_ONLY;
			break;
		case 'h':
			argp_state_help(st, st->out_stream, ARGP_HELP_STD_HELP);
			break;
		case ARGP_KEY_NO_ARGS:
			break;
		default:
			return ARGP_ERR_UNKNOWN;
	}

	return 0;
};

static struct argp argp = {opt, parse_opt, args_doc, doc};

static int write_non_present_param(cur_task *a)
{
	s_dict *entry = a->dict;

	while (entry->name != NULL) {
		if (entry->is_set == 0 && strncmp(BOOTMENU_ENT, entry->name, sizeof(BOOTMENU_ENT) - 1)) {
			int l = strlen(entry->value) + 1;
			fwrite(entry->value, 1, l, a->tmp);
			entry->is_set = 1;
		};

		if (entry->next == NULL)
			break;

		entry = entry->next;
	};

	return 0;
};

static int write_bootmenu_entries(cur_task *a)
{
	s_dict *entry = a->dict;

	while (entry->name != NULL) {
		if (entry->is_set == 0 && !strncmp(BOOTMENU_ENT, entry->name, sizeof(BOOTMENU_ENT) - 1)) {
			int l = strlen(entry->value) + 1;
			fwrite(entry->value, 1, l, a->tmp);
			entry->is_set = 1;
		};

		if (entry->next == NULL)
			break;

		entry = entry->next;
	};


	return 0;
};

/**
 * Check, if current parameter is set in dict list.
 * Return value is equal zero, if no such parameter is set,
 * > 0 if param find and return its length
 * < 0 if param must be dropped
 * @param buf current buffer point
 * @param ls is line start
 * @param e pointer to list of parameters that set in config file
 * @return > 0, 0, < 0
 */
static int check_and_set(char *buf, int ls, s_dict **e)
{
	char *end = strchr(&buf[ls], '=');
	int ln = 0;
	s_dict *entry;
	entry = *e;

	*end = '\0';

	// drop bootmenu entry
	if ( strncmp(BOOTMENU_ENT, &buf[ls], sizeof(BOOTMENU_ENT) - 1) == 0 &&
	     *(end - 1) >= '0' &&
	     *(end - 1) <= '9') {
		return -1;
	};

	while (entry->name != NULL) {
		if (strcmp(entry->name, &buf[ls]) == 0 && entry->value != NULL) {
			// some params must be dropped
			if (entry->is_set > 0)
				return -1;
			ln = strlen(entry->value) + 1; // last character 0 must be written
			*e = entry;
			break;
		};

		if (entry->next == NULL)
			break;

		entry = entry->next;
	};

	*end = '=';

	return ln;
}


static int read_env(cur_task *a)
{
	size_t r_data, w_data;
	int size  = getpagesize(), z, ls;
	char *buf;
	DO((buf = malloc(size)) == 0, 0, "malloc");

	DO(fseek(a->disk,  CRC_SIZE, SEEK_CUR) < 0, 0, "fseek");
	if (a->o == RTW) {
		char i[CRC_SIZE] = {0};
		fwrite(i, 1, CRC_SIZE, a->tmp);
	};

	z = ls = 0;
	do {
		int i = ls = 0;
		DO((r_data = fread(buf, 1, size, a->disk)) < 0, 0, "reading disk");

		for (w_data = 0; i < r_data; i++) {
			if (buf[i] == 0) {
				z++;

				if (z >= 2) {
					ls = i;
					buf[i] = a->o == READ_ONLY ? '\n' : '\0';
					r_data = 0; //  two '0' bytes mean, that there is no more valid data. just stop reading
				} else {

					// If operation READ_ONLY, then replace 0 to newline and write in file
					if (a->o == READ_ONLY) {
						buf[i] = '\n';
					};

					// Substitute arguments for RTW
					if (a->o == RTW) {
						s_dict *entry = a->dict;
						int l = check_and_set(buf, ls, &entry);

						if (l > 0) {
							debug("substitute parameter %s\n", entry->value);
							w_data += fwrite(entry->value, 1, l, a->tmp);
							entry->is_set = 1;
							continue;
						};

						if (l < 0) {
							continue;
						};
					};
				};

				w_data += fwrite(&buf[ls], 1, i - ls + 1, a->tmp);
				continue;
			};

			if (z) {
				ls = i;
			}

			z = 0;
		};

		DO(fseek(a->disk,  (int)ls - i, SEEK_CUR) < 0, 0, "fseek");

		debug("r = %d; w = %d; i = %d; ls = %d; eof = %x; ftell = %d\n", r_data, w_data, i, ls, feof(a->disk), ftell(a->disk));
	} while ((r_data != 0) && (!feof(a->disk)));

	return EXIT_SUCCESS;
};

static int filter(const struct dirent *dir)
{
//	debug("dir->d_name = %s\n", dir->d_name);
	if (!strncmp(dir->d_name, "part@", 5))
		return 1;

	return 0;
};

/**
 * Search a->sn node in device tree and set its offset and size
 * @param a : a->sn partition name.
 * @return 0 on success; 1 otherwise
 */
static int get_section(cur_task *a)
{
	struct dirent **namelist;
	int n;
	char *type = a->sn;
	int ret = EXIT_FAILURE;

//	debug("Searching %s partition\n", type);

	if (strcmp(type, SEC_ROM) == 0) {
		fseek(a->disk, 0L, SEEK_END);
		a->p = 0;
		a->s = ftell(a->disk);
		return EXIT_SUCCESS;
	};

	DO((n = scandir(OF_FLASH_DEVICE, &namelist, filter, alphasort)) < 0, 0, "scandir");
	for (int i = 0; i < n; i++) {
		unsigned long lp, ld = strlen(namelist[i]->d_name);
		lp = sizeof(OF_FLASH_DEVICE) + ld + 1 + sizeof("label"); // full path + dir name + '/' + label
		char *file_path = malloc(lp), data[MAX_DATA_LENGTH], *of;
		FILE *f1, *f2;

		of = mempcpy(file_path, OF_FLASH_DEVICE, sizeof(OF_FLASH_DEVICE) - 1);
		of = mempcpy(of, namelist[i]->d_name, ld);
		*(of++) = '/';
		mempcpy(of, "label", sizeof("label"));

		DO((f1 = fopen(file_path, "r")) == NULL, 0, file_path);
		for (int j = 0; !feof(f1) && j < MAX_DATA_LENGTH; j++)
			data[j] = fgetc(f1);

//		debug("Compare %s and %s\n", data, type);
		if (strcmp(data, type) != 0)
			goto free;

		mempcpy(of, "reg", sizeof("reg"));
		DO((f2 = fopen(file_path, "r")) == NULL, 0, file_path);
		for (int j = 0; !feof(f2) && j < MAX_DATA_LENGTH; j++)
			data[j] = fgetc(f2);

		a->s = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];
		DO(a->s == 0, ENOSPC, "Partition size can't be zero!");
		a->p = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];

//		debug("a->s = %x; a->p = %x\n", a->s, a->p);

		// partition is find
		i = n;
		ret = EXIT_SUCCESS;
		fclose(f2);
free:
		fclose(f1);
		free(file_path);
	};

	while(n--) {
//		debug("%s\n", namelist[n]->d_name);
		free(namelist[n]);
	}
	free(namelist);

	return ret;
};

int main(int argc, char *argv[])
{
	//By default: no args, read uboot.conf and write it in to the ENV partition
	cur_task a = {
		.cfg_file = CFG_FILE,
		.tmp_file = TMP_FILE,
		.disk_file = FLASH,
		.o = RTW,
		.sn = SEC_ENV,
		.p = 0,
		.s = 0,
		.dict = NULL
	};


	argp_parse(&argp, argc, argv, 0, 0, &a);
	debug("Current task: %s\n", a.o == RTW ? "RTW" : a.o == READ_ONLY ? "READ_ONLY" : "WRITE_ONLY" );

	if (a.cfg_file)
		DO((a.cfg = fopen(a.cfg_file, "r")) == NULL, 0, a.cfg_file);

	if (a.tmp_file)
		DO((a.tmp = fopen(a.tmp_file, "w+")) == NULL, 0, a.tmp_file);

	DO((a.disk = fopen(a.disk_file, "r+b")) == NULL, 0, a.disk_file);

	DO(get_section(&a) == EXIT_FAILURE, 0, NULL);

	// Read ENSET from mtdblock
	if (a.o == READ_ONLY || a.o == RTW) {
		DO(fseek(a.disk,  a.p, SEEK_SET) < 0, 0, "fseek");

		// Preread actions for RTW
		// Parse CFG_FILE and create list 'dict' with known parameters
		if (a.o == RTW) {
			yyparse(&a);
			s_dict *entry = a.dict;
			while (entry != NULL && entry->name != NULL) {
				debug("param %s with val %s\n", entry->name, entry->value);
				entry = entry->next;
			};
			debug("%s", "=====================\n");
		};

		DO(read_env(&a) == EXIT_FAILURE, 0, "read_env");

		// Postread actions for RTW
		// Write params that doesn't present in ENVSET partition and bootmenu entries
		if (a.o == RTW) {
			DO(fseek(a.tmp, -1, SEEK_END) < 0, 0, "fseek");
			write_non_present_param(&a);
			write_bootmenu_entries(&a);

			int sz = ftell(a.tmp);
			char *buf = malloc(a.s);

			fseek(a.tmp, 0, SEEK_SET);
			fread(buf, 1, sz, a.tmp);
			memset(buf + sz + 1, 0, a.s - sz); // buf[sz - 1] == end of line; buf[sz] == end of param list;
			uLong crc = crc32(0L, buf + CRC_SIZE, a.s - CRC_SIZE);
			memcpy(buf, &crc, CRC_SIZE);

			fseek(a.tmp, 0, SEEK_SET);
			fwrite(buf, 1, a.s, a.tmp);
			free(buf);
		};
	};

	// Write any data to mtdblock
	if (a.o == WRITE_ONLY || a.o == RTW) {
		FILE *f;
		int sz;

		if (a.o == RTW)
			f = a.tmp;
		else
			f = a.cfg;

		// size check
		fseek(f, 0L, SEEK_END);
		sz = ftell(f);

		if (sz > a.s) {
			printf("File %s is too large for sector %u! Abort\n", a.o == RTW ? a.tmp_file : a.cfg_file, a.p);
			goto out;
		};

		DO(fseek(f,  0, SEEK_SET) < 0, 0, "fseek");
		DO(fseek(a.disk,  a.p, SEEK_SET) < 0, 0, "fseek");

		debug("writing from file %s by offset %x and partition size %x\n", f == a.tmp ? a.tmp_file : a.cfg_file, a.p, a.s);
		char *data = malloc(sz);
		DO(fread(data, 1, sz, f) < 0, 0, "Error while reading from input file!");
		DO(fwrite(data, 1, sz, a.disk) < 0, 0, "Error while writing on disk!");
		free(data);
	};

out:
	if (a.tmp) {
		DO(fclose(a.tmp) < 0, 0, "close tmp file");
//		if (a.o == RTW)
//			remove(a.tmp_file);
	};

	if (a.cfg) {
		DO(fclose(a.cfg) < 0, 0, "close config");
	};
	DO(fclose(a.disk) < 0, 0, "close disk");

	return EXIT_SUCCESS;
};
