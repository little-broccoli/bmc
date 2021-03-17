/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Karamov Artur <karamov@fastwel.ru>
 */

#ifndef UPDATE_UBOOT_HEAD
#define UPDATE_UBOOT_HEAD

#ifndef PROGNAME
# define PROGNAME __BASE_FILE__
#endif

#ifndef VERSION
# define VERSION "unknown"
#endif

#ifdef DEBUG
# define debug(fmt, ...) printf("%s:%s:%d: " fmt, __FILE__, __func__, __LINE__, __VA_ARGS__)
#else
# define debug(fmt, ...)
#endif

#define ARRAY_SIZE(x)	(sizeof(x)/sizeof(x[0]))
typedef struct struct_dict {
	char *name;
	char *value;
	char is_set;
	struct struct_dict *next;
} s_dict;

enum operation {
	WRITE_ONLY = 0,		// Write in ROM
	READ_ONLY,		// Read from ROM
	RTW			// Read then write
};

/**
 * @struct cur_task
 * Information for current task
 */
typedef struct cur_task {
	char *cfg_file, *disk_file, *tmp_file; 	/**< Paths to config, flash disk and temporary file correspondingly */
	FILE *cfg, *disk, *tmp;			/**< Actual file pointers */
	enum operation o;			/**< Current operation */
	char *sn;				/**< Section name */
	uint32_t p;				/**< Offset for partition in flash disk */
	uint32_t s;				/**< Partition size */
	s_dict *dict;
} cur_task;

// Available parameters name:
#define BOOTDELAY 	"bootdelay"
#define BOOTMENU_DEF	"bootmenu_default"
#define	UBOOT_SILENT	"silent"

#define BOOTMENU_ENT		"bootmenu_"
#define BOOTMENU_LINE		"boot_line_"
#define INTERFACE_LINE		"interface_"
#define KERNEL_LINE		"kernel_load_"
#define INITRD_LINE		"initrd_load_"
#define FDT_LINE		"fdt_"
#define KERNEL_ARGS_LINE	"kernel_args_"

// default entries
#define KERN_ARGS		" setenv bootargs ${bootargs} "

#define SATA_INIT		" setenv sata_port 0; sata init; sleep 1"
#define SATA_KERNEL		" ext4load sata 0:1 ${kernel_addr_n} "
#define SATA_INITRD1		" ext4load sata 0:1 ${initrd_addr_n} "
#define SATA_INITRD2		"; setenv initrd_len ${filesize}"
#define SATA_FDT1		" ext4load sata 0:1 ${fdt_addr_n} "
#define SATA_FDT2		"; fdt addr ${fdt_addr_n}"

#define USB_INIT		" usb start; sleep 1"
#define USB_KERNEL		" ext4load usb 0:1 ${kernel_addr_n} "
#define USB_INITRD1		" ext4load usb 0:1 ${initrd_addr_n} "
#define USB_INITRD2		"; setenv initrd_len ${filesize}"
#define USB_FDT1		" ext4load usb 0:1 ${fdt_addr_n} "
#define USB_FDT2 		"; fdt addr ${fdt_addr_n}"

#define NET_INIT		" "
#define NET_KERNEL1		" setenv loadaddr ${kernel_addr_n}; setenv bootfile "
#define NET_KERNEL2		"; tftp"
#define NET_INITRD1		" setenv loadaddr ${initrd_addr_n}; setenv bootfile "
#define NET_INITRD2		"; tftp; setenv initrd_len ${filesize}"
#define NET_FDT1		" setenv loadaddr ${fdt_addr_n}; setenv bootfile "
#define NET_FDT2		"; tftp; fdt addr ${fdt_addr_n}; fdt addr ${fdt_addr_n}"

#define BOOTMENU_SPI_FLASH	"Boot from SPI Flash to minimal FS (rom)=run flash_boot_ram"
#define BOOTMENU_JUMPERS	"Boot according to jumpers=boot_jumper"

#endif
