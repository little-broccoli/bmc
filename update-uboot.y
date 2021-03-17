/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Karamov Artur <karamov@fastwel.ru>
 */

%verbose
%define parse.trace

%code requires {
  #include "update-uboot.h"
}

%parse-param {cur_task *ct}

%initial-action
{
  s_dict *e;
  e = ct->dict = malloc(sizeof(*ct->dict));
  
  e->name = NULL;
  e->value = NULL;
  e->is_set = 0;
  e->next = NULL;

  //yydebug = 1;
  yyin = ct->cfg;
};

%{
//#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "update-uboot.h"

extern int yylex();
extern int yyparse();
extern FILE *yyin;

// u-boot/common/cmd_bootmenu.c: MAX_COUNT 99
#define MAX_COUNT 99
#define MAX_ITER_ARRAY 3
static unsigned int num = 0;
static char snum[MAX_ITER_ARRAY] = "0";

static enum type {
  SATA = 0,
  USB,
  NET
} cur_type;

#define MAX_PARAMS_IN_BOOTLINE 6
#define CBE_TYPE 0
#define CBE_KERNEL 1
#define CBE_INITRD 2
#define CBE_FDT 3
#define CBE_KERNEL_ARGS 4
#define CBE_BOOTCMD 5

static char *cbe[MAX_PARAMS_IN_BOOTLINE] = {NULL}; /**< Current bootline entry */

void yyerror(cur_task *ct, const char *s);
char *concat(char *s1, char *s2);
int set_param(cur_task *ct, char *s, char *v, int drop);

char *create_interface_line();
char *create_kernel_line(char *s);
char *create_kernel_args_line(char *s);
char *create_initrd_line(char *s);
char *create_fdt_line(char *s);
char *create_boot_line(char *s);
void add_default_bootmenu(cur_task *ct);
%}

%union {
  char *sval;
}

%token <sval> YESNO DIGIT STR_VALUE STR_ALNUM

//common
%token BLANK

//for uboot
%token TIMEOUT DEFAULT LABEL SILENT

//for kernel
%token LINUX APPEND INITRD FDT
%token <sval> TYPE

%type <sval> String_line

%%
file:
  Head
  Menu			{ add_default_bootmenu(ct); }
  ;

Head:
  Head H_line
  | H_line
  ;

H_line:
  TIMEOUT DIGIT		{ set_param(ct, BOOTDELAY, $DIGIT, 0); free($DIGIT); }
  | DEFAULT STR_ALNUM	{ set_param(ct, BOOTMENU_DEF, $STR_ALNUM, 0); free($STR_ALNUM); }
  | SILENT YESNO	{ set_param(ct, UBOOT_SILENT, $YESNO, *$YESNO == '1' ? 1: 0); free($YESNO); }
  | BLANK
  ;

Menu:
  Menu BLANK
  | Menu M_entry	{ num++; sprintf(snum, "%d", num); }
  | M_entry		{ num++; sprintf(snum, "%d", num); }
  ;

M_entry:
  LABEL STR_ALNUM 	{  if (num > MAX_COUNT)
			      yyerror(ct, "To many entries: max is 99");
			  
			  int ll = strlen($STR_ALNUM);
			  char *vs = malloc(sizeof("Boot ") + sizeof(snum) + sizeof(".  ") + ll + sizeof(" = run ") + sizeof(BOOTMENU_LINE) + sizeof(snum));
			  strcpy(vs, "Boot ");
			  strcat(vs, snum);
			  strcat(vs, ".  ");
			  strcat(vs, $STR_ALNUM);
			  strcat(vs, " = run ");
			  strcat(vs, BOOTMENU_LINE);
			  strcat(vs, snum);
			  
			  char *s = concat(BOOTMENU_ENT, snum);
			  
			  set_param(ct, s, vs, 0);
			  
			  s_dict *entry = ct->dict;
			  while (entry->name != NULL) {
			    if(strcmp(entry->name, BOOTMENU_DEF) == 0) {
			      char *start = strchr(entry->value, '=');
			      start++;
			      if(strcmp(start, $STR_ALNUM) == 0) {
				set_param(ct, BOOTMENU_DEF, snum, 0);
				break;
			      };
			    };

			    if (entry->next == NULL)
			      break;
    
			    entry = entry->next;
			  };
			  
			  free(vs); }
  M_lines		{ char *s = concat(BOOTMENU_LINE, snum);
			  cbe[CBE_BOOTCMD] = "all_bootnr";
			  char *vs = create_boot_line($STR_ALNUM); 
			  
			  set_param(ct, s, vs, 0);
			  free(vs); free($STR_ALNUM); }
  ;

M_lines:
  M_lines M_line
  | M_line
  ;

M_line:
  TYPE				{ cur_type = strcmp($TYPE, "sata") == 0 ? SATA : strcmp($TYPE, "usb") == 0 ? USB : NET;
				  char *s = concat(INTERFACE_LINE, snum);
				  char *vs = create_interface_line();

				  if (vs != NULL) {
				    set_param(ct, s, vs, 0);
				    cbe[CBE_TYPE] = s;
				  } else
				    cbe[CBE_TYPE] = NULL;

				  free(vs); free($TYPE); }
  | LINUX STR_VALUE		{ char *s = concat(KERNEL_LINE, snum);
				  char *vs = create_kernel_line($STR_VALUE);
				  
				  set_param(ct, s, vs, 0);
				  cbe[CBE_KERNEL] = s;
				  
				  free(vs); free($STR_VALUE); }
  | APPEND String_line		{ char *s = concat(KERNEL_ARGS_LINE, snum); 
				  char *vs = create_kernel_args_line($String_line);

				  set_param(ct, s, vs, 0);
				  cbe[CBE_KERNEL_ARGS] = s;
				  free(vs); free($String_line); }
  | INITRD STR_VALUE		{ char *s = concat(INITRD_LINE, snum);
				  char *vs = create_initrd_line($STR_VALUE);
  
				  set_param(ct, s, vs, 0);
				  cbe[CBE_INITRD] = s;
				  free(vs); free($STR_VALUE); }
  | FDT STR_VALUE		{ char *s = concat(FDT_LINE, snum);
				  char *vs = create_fdt_line($STR_VALUE);

				  set_param(ct, s, vs, 0);
				  cbe[CBE_FDT] = s;
				  free(vs); free($STR_VALUE); }
  ;

String_line:
  String_line STR_VALUE		{ int ln = strlen($1) + 2 + strlen($2);
				  char blank[2] = " ";
				  char *s = strdup($$);
				  $$ = realloc($$, ln);
				  
				  strcpy($$, s);
				  strcat($$, blank);
				  strcat($$, $2);
				  free(s);
				}
  | String_line STR_ALNUM	{ int ln = strlen($1) + 2 + strlen($2);
				  char blank[2] = " ";
				  char *s = strdup($$);
				  $$ = realloc($$, ln);
				  
				  strcpy($$, s);
				  strcat($$, blank);
				  strcat($$, $2);
				  free(s);
				}
  | STR_ALNUM			{ $$ = $1; }
  | STR_VALUE			{ $$ = $1; }
  ;
%%

// Here is defined some auxiliary code

/**
 * Concatinate s1 and s2 string in new allocated string
 */
char *concat(char *s1, char *s2)
{
  uint32_t ln1, ln2;
  char *str = NULL;
  
  if (s1 == NULL || s2 == NULL)
    return str;
    
  ln1 = strlen(s1), ln2 = strlen(s2);
  str = malloc(ln1 + ln2);
  
  strcpy(str, s1);
  strcat(str, s2);
  
  return str;
};

/**
 * Add parameter s with value v in ct->dict.
 * Don't free s - it's will be used later.
 * Pointer v may be freed.
 */
int set_param(cur_task *ct, char *s, char *v, int drop)
{
  s_dict *entry = ct->dict;
  uint32_t ln1 = strlen(s), ln2 = strlen(v);
  char *str = malloc(ln1 + 1 + ln2 + 1);
  memcpy(str, s, ln1);
  *(str + ln1) = '=';
  memcpy(str + ln1 + 1, v, ln2);
  *(str + ln1 + 1 + ln2) = '\x0';
  
  while (entry->name != NULL) {
    if(strcmp(entry->name, s) == 0) {
      free(entry->value);
      entry->value = str;
      return 0;
    };
 
    if (entry->next == NULL)
      break;
    
    entry = entry->next;
  };
  
  // for the first entry, mem is already allocated
  if (entry->name != NULL) {
    entry->next = malloc(sizeof(*entry));
    entry = entry->next;
  }

  entry->name = s;
  entry->value = str;
  entry->is_set = drop ? 1: 0;
  entry->next = NULL;

  return 0;
};

char *create_interface_line()
{
  char *str;
  
  switch (cur_type) {
    case SATA:
      str = strdup(SATA_INIT);
      break;
    
    case USB:
      str = strdup(USB_INIT);
      break;
      
    case NET:
      str = NULL;
      break;

    default:
      str = NULL;
      break;
  };
  
  return str;
};

char *create_kernel_line(char *s)
{
  char *str;
  int ln;
  
  switch (cur_type) {
    case SATA:
      ln = sizeof(SATA_KERNEL) + strlen(s);
      str = malloc(ln);
      strcpy(str, SATA_KERNEL);
      strcat(str, s);
      break;
    
    case USB:
      ln = sizeof(USB_KERNEL) + strlen(s);
      str = malloc(ln);
      strcpy(str, USB_KERNEL);
      strcat(str, s);
      break;
      
    case NET:
      ln = sizeof(NET_KERNEL1) + strlen(s) + sizeof(NET_KERNEL2);
      str = malloc(ln);
      strcpy(str, NET_KERNEL1);
      strcat(str, s);
      strcat(str, NET_KERNEL2);
      break;

    default:
      str = NULL;
      break;
  };
  
  return str;
};

char *create_kernel_args_line(char *s)
{
  char *str;
  int ln = sizeof(KERN_ARGS) + strlen(s);

  str = malloc(ln);
  strcpy(str, KERN_ARGS);
  strcat(str, s);
  
  return str;
};

char *create_initrd_line(char *s)
{
  char *str;
  int ln;
  
  switch (cur_type) {
    case SATA:
      ln = sizeof(SATA_INITRD1) + strlen(s) + sizeof(SATA_INITRD2);
      str = malloc(ln);
      strcpy(str, SATA_INITRD1);
      strcat(str, s);
      strcat(str, SATA_INITRD2);
      break;
    
    case USB:
      ln = sizeof(USB_INITRD1) + strlen(s) + sizeof(USB_INITRD2);
      str = malloc(ln);
      strcpy(str, USB_INITRD1);
      strcat(str, s);
      strcat(str, USB_INITRD2);
      break;
      
    case NET:
      ln = sizeof(NET_INITRD1) + strlen(s) + sizeof(NET_INITRD2);
      str = malloc(ln);
      strcpy(str, NET_INITRD1);
      strcat(str, s);
      strcat(str, NET_INITRD2);
      break;

    default:
      str = NULL;
      break;
  };
  
  return str;
}

char *create_fdt_line(char *s)
{
  char *str;
  int ln;
  
  switch (cur_type) {
    case SATA:
      ln = sizeof(SATA_FDT1) + strlen(s) + sizeof(SATA_FDT2);
      str = malloc(ln);
      strcpy(str, SATA_FDT1);
      strcat(str, s);
      strcat(str, SATA_FDT2);
      break;
    
    case USB:
      ln = sizeof(USB_FDT1) + strlen(s) + sizeof(USB_FDT2);
      str = malloc(ln);
      strcpy(str, USB_FDT1);
      strcat(str, s);
      strcat(str, USB_FDT2);
      break;
      
    case NET:
      ln = sizeof(NET_FDT1) + strlen(s) + sizeof(NET_FDT2);
      str = malloc(ln);
      strcpy(str, NET_FDT1);
      strcat(str, s);
      strcat(str, NET_FDT2);
      break;

    default:
      str = NULL;
      break;
  };
  
  return str;
}

char *create_boot_line(char *s)
{
  int i = 0, j = 0;
  while (i < MAX_PARAMS_IN_BOOTLINE) {
    if (cbe[i] != NULL)
      j++;
    i++;
  };
  
  if (cur_type == NET)
    j++; // == MAX_PARAMS_IN_BOOTLINE - 1, e.g. TYPE = NULL

  if (j < MAX_PARAMS_IN_BOOTLINE) {
    char str[200];
    sprintf(str, "%s %s\n", "Not enouth parameters set for entry", s);
    yyerror(NULL, str);
  };
  
  int ln = 0;
  char *str;
  
  if (cur_type != NET)
    ln += strlen(cbe[CBE_TYPE]);
    
  ln += strlen(cbe[CBE_KERNEL]) 
	+ strlen(cbe[CBE_INITRD]) 
	+ strlen(cbe[CBE_FDT]) 
	+ strlen(cbe[CBE_KERNEL_ARGS]) 
	+ strlen(cbe[CBE_BOOTCMD]) 
	+ j * sizeof(" run ;");

  str = malloc(ln);
  if (cur_type == NET)
    i = 1;
  else
    i = 0;

  *str = '\0';
  while (i < MAX_PARAMS_IN_BOOTLINE) {
    strcat(str, " run ");
    strcat(str, cbe[i]);
    strcat(str, ";");
    i++;
  };
  
  return str;
};

void add_default_bootmenu(cur_task *ct)
{
  // 2 default entries: SPI rom and jumpers
  char *s[2];
  char vs[sizeof("Boot ") + sizeof(snum) + sizeof(".  ") + sizeof(BOOTMENU_SPI_FLASH)];
  s_dict *entry = ct->dict;

  s[0] = malloc(sizeof(BOOTMENU_ENT) + sizeof(snum));
  s[1] = malloc(sizeof(BOOTMENU_ENT) + sizeof(snum));

  strcpy(s[0], BOOTMENU_ENT);
  strcat(s[0], snum);
  while (entry->name != NULL) {
    if (strcmp(entry->name, s[0]) == 0) {
      num++; 
      sprintf(snum, "%d", num);
      s[0][sizeof(BOOTMENU_ENT) - 1] = '\0';
      strcat(s[0], snum);
      break;
    };
    
    if (entry->next == NULL)
      break;
    
    entry = entry->next;
  };
  
  strcpy(vs, "Boot ");
  strcat(vs, snum);
  strcat(vs, ".  ");
  strcat(vs, BOOTMENU_SPI_FLASH);

  set_param(ct, s[0], vs, 0);
  
  num++;
  sprintf(snum, "%d", num);
  strcpy(s[1], BOOTMENU_ENT);
  strcat(s[1], snum);
  
  vs[sizeof("Boot ") + sizeof(snum) + sizeof(".  ") - 1] = '\0';
  strcat(vs, BOOTMENU_JUMPERS);
  set_param(ct, s[1], vs, 0);
};

void yyerror(cur_task *ct, const char *s)
{
  printf("Ups! error! %s\n", s);
  exit(-1);
}
