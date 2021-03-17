/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Karamov Artur <karamov@fastwel.ru>
 */

%{
#include <stdio.h>
#include "update-uboot.bison.h"
%}

%option noyywrap

%%
^[[:blank:]]*\n		{return (BLANK);}
[ \t\n]			{}
\/\/.*$			{}
"TYPE "(usb|sata|tftp)	{yylval.sval = strdup(yytext + 5); return (TYPE);}
"TIMEOUT"		{return (TIMEOUT);}
"DEFAULT"		{return (DEFAULT);}
"LABEL"			{return (LABEL);}
"LINUX"			{return (LINUX);}
"APPEND"		{return (APPEND);}
"INITRD"		{return (INITRD);}
"SILENT"		{return (SILENT);}
"FDT"			{return (FDT);}
YES|NO			{ if (strcmp(yytext, "YES") == 0) {
			    yylval.sval = strdup("1");
			  } else 
			    yylval.sval = strdup("0");
			  return (YESNO);
			}
[[:digit:]]+		{ yylval.sval = strdup(yytext); return (DIGIT);}
[[:alnum:]]+		{ yylval.sval = strdup(yytext); return (STR_ALNUM);}
[[:alnum:]=/.:,\"_-]+	{ yylval.sval = strdup(yytext); return (STR_VALUE);}
%%
