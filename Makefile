PRG ?= update-uboot
BISON_O = $(PRG).bison
FLEX_O = $(PRG).lex

ifeq ($(shell command -v git &> /dev/null),)
VERSION = 1.2
else
VERSION = $(shell git tag | tail -1)
endif
DEBUG ?=
CFLAGS = -DPROGNAME="$(PRG)" -DVERSION="$(VERSION)"

all: bison flex
	@gcc $(CFLAGS) $(BISON_O).c $(FLEX_O).c $(PRG).c -o $(PRG) -lz

debug: bison flex
	@gcc -DDEBUG -g $(BISON_O).c $(FLEX_O).c $(PRG).c -o $(PRG) -lz


flex:
	@flex -o $(FLEX_O).c --header-file=$(FLEX_O).h $(PRG).lex

bison:
	@bison -o $(BISON_O).c -d $(PRG).y

clean:
	@rm $(PRG) $(FLEX_O).c $(FLEX_O).h $(BISON_O).c $(BISON_O).h $(BISON_O).output

