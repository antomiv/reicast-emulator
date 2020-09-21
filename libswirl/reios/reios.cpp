/*

	Rev :

	Task : reios_sys_misc   ??

	Sonic loop : Ints ??

*/

/*
	Extremely primitive bios replacement

	Many thanks to Lars Olsson (jlo@ludd.luth.se) for bios decompile work
		http://www.ludd.luth.se/~jlo/dc/bootROM.c
		http://www.ludd.luth.se/~jlo/dc/bootROM.h
		http://www.ludd.luth.se/~jlo/dc/security_stuff.c
*/
#include "license/bsd"
#include "reios.h"
#include "reios_utils.h"
#include "reios_elf.h"

#include "gdrom_hle.h"
#include "descrambl.h"
#include "hw/sh4/sh4_mem.h"
#include "hw/naomi/naomi_cart.h"
#include <map>
#include "reios_syscalls.h"

//#define debugf printf

#define debugf(...) 
//printf(__VA_ARGS__)

reios_context_t g_reios_ctx;
extern unique_ptr<GDRomDisc> g_GDRDisc;


//Read 32 bit 'bi-endian' integer
//Uses big-endian bytes, that's what the dc bios does too
u32 read_u32bi(u8* ptr) {
	return (ptr[4]<<24) | (ptr[5]<<16) | (ptr[6]<<8) | (ptr[7]<<0);
}

bool reios_locate_bootfile(const char* bootfile="1ST_READ.BIN") {
	u32 data_len = 2048 * 1024;
	u8* temp = new u8[data_len];

	 
	g_GDRDisc->ReadSector(temp, g_reios_ctx.base_fad + 16, 1, 2048);

	if (memcmp(temp, "\001CD001\001", 7) == 0) {
		printf("reios: iso9660 PVD found\n");
		u32 lba = read_u32bi(&temp[156 + 2]); //make sure to use big endian
		u32 len = read_u32bi(&temp[156 + 10]); //make sure to use big endian
		
		data_len = ((len + 2047) / 2048) *2048;

		printf("reios: iso9660 root_directory, FAD: %d, len: %d\n", 150 + lba, data_len);
		g_GDRDisc->ReadSector(temp, 150 + lba, data_len/2048, 2048);
	}
	else {
		g_GDRDisc->ReadSector(temp, g_reios_ctx.base_fad + 16, data_len / 2048, 2048);
	}

	for (int i = 0; i < (data_len-20); i++) {
		if (memcmp(temp+i, bootfile, strlen(bootfile)) == 0){
			printf("Found %s at %06X\n", bootfile, i);

			u32 lba = read_u32bi(&temp[i - 33 +  2]); //make sure to use big endian
			u32 len = read_u32bi(&temp[i - 33 + 10]); //make sure to use big endian
			
			printf("filename len: %d\n", temp[i - 1]);
			printf("file LBA: %d\n", lba);
			printf("file LEN: %d\n", len);

			if (g_reios_ctx.descrambl)
				descrambl_file(lba + 150, len, GetMemPtr(0x8c010000, 0));
			else
				g_GDRDisc->ReadSector(GetMemPtr(0x8c010000, 0), lba + 150, (len + 2047) / 2048, 2048);

			g_reios_ctx.sync_sys_cfg();
			/*if (false) {
				FILE* f = fopen("z:\\1stboot.bin", "wb");
				fwrite(GetMemPtr(0x8c010000, 0), 1, len, f);
				fclose(f);
			}*/

			delete[] temp;

			return true;
		}
	}

	delete[] temp;
	return false;
}
 
void reios_pre_init()
{
	if (g_reios_ctx.pre_init)
		return;

	if (g_GDRDisc->GetDiscType() == GdRom) {
		g_reios_ctx.base_fad = 45150;
		g_reios_ctx.descrambl = false;
	} else {
		u8 ses[6];
		g_GDRDisc->GetSessionInfo(ses, 0);
		g_GDRDisc->GetSessionInfo(ses, ses[2]);
		g_reios_ctx.base_fad = (ses[3] << 16) | (ses[4] << 8) | (ses[5] << 0);
		g_reios_ctx.descrambl = true;
	}
	g_reios_ctx.pre_init = true;
}

char* reios_disk_id() {

	reios_pre_init();

	g_GDRDisc->ReadSector(GetMemPtr(0x8c008000, 0), g_reios_ctx.base_fad, 256, 2048);
	memset(g_reios_ctx.ip_bin, 0, sizeof(g_reios_ctx.ip_bin));
	memcpy(g_reios_ctx.ip_bin, GetMemPtr(0x8c008000, 0), 256);
	memcpy(&g_reios_ctx.reios_hardware_id[0], &g_reios_ctx.ip_bin[0], 16 * sizeof(char));
	memcpy(&g_reios_ctx.reios_maker_id[0], &g_reios_ctx.ip_bin[16],   16 * sizeof(char));
	memcpy(&g_reios_ctx.reios_device_info[0], &g_reios_ctx.ip_bin[32],   16 * sizeof(char));
	memcpy(&g_reios_ctx.reios_area_symbols[0], &g_reios_ctx.ip_bin[48],   8 * sizeof(char));
	memcpy(&g_reios_ctx.reios_peripherals[0], &g_reios_ctx.ip_bin[56],   8 * sizeof(char));
	memcpy(&g_reios_ctx.reios_product_number[0], &g_reios_ctx.ip_bin[64],   10 * sizeof(char));
	memcpy(&g_reios_ctx.reios_product_version[0], &g_reios_ctx.ip_bin[74],   6 * sizeof(char));
	memcpy(&g_reios_ctx.reios_releasedate[0], &g_reios_ctx.ip_bin[80],   16 * sizeof(char));
	memcpy(&g_reios_ctx.reios_boot_filename[0], &g_reios_ctx.ip_bin[96],   16 * sizeof(char));
	memcpy(&g_reios_ctx.reios_software_company[0], &g_reios_ctx.ip_bin[112],   16 * sizeof(char));
	memcpy(&g_reios_ctx.reios_software_name[0], &g_reios_ctx.ip_bin[128],   128 * sizeof(char));

	g_reios_ctx.identify_disc_type();
	return g_reios_ctx.reios_product_number;
}

const char* reios_locate_ip() {

	reios_pre_init();

	printf("reios: loading ip.bin from FAD: %d\n", g_reios_ctx.base_fad);

	g_GDRDisc->ReadSector(GetMemPtr(0x8c008000, 0), g_reios_ctx.base_fad, 16, 2048);
	
	memset(g_reios_ctx.reios_bootfile, 0, sizeof(g_reios_ctx.reios_bootfile));
	memcpy(g_reios_ctx.reios_bootfile, GetMemPtr(0x8c008060, 0), 16);

	printf("reios: bootfile is '%s'\n", g_reios_ctx.reios_bootfile);

	for (int i = 15; i >= 0; i--) {
		if (g_reios_ctx.reios_bootfile[i] != ' ')
			break;
		g_reios_ctx.reios_bootfile[i] = 0;
	}
	return g_reios_ctx.reios_bootfile;
}


void reios_sys_system() {
	debugf("reios_sys_system\n");
	Sh4cntx.pc = Sh4cntx.pr;
	u32 cmd = Sh4cntx.r[7];

	switch (cmd) {
		case 0:	//SYSINFO_INIT
		{
			g_reios_ctx.sync_sys_cfg();
			Sh4cntx.r[0] = 0;
		}
			break;

		case 2: //SYSINFO_ICON 
		{
			printf("SYSINFO_ICON\n");
			/*
				r4 = icon number (0-9, but only 5-9 seems to really be icons)
				r5 = destination buffer (704 bytes in size)
			*/
			Sh4cntx.r[0] = 704;
		}
		break;

		case 3: //SYSINFO_ID 
		{
			//WriteMem32(SYSINFO_ID_ADDR + 0, 0xe1e2e3e4);
			//WriteMem32(SYSINFO_ID_ADDR + 4, 0xe5e6e7e8);

			Sh4cntx.r[0] = SYSINFO_ID_ADDR2;
		}
		break;

		default:
			printf("unhandled: reios_sys_system\n");
			break;
	}
}

void reios_sys_font() {
	printf("reios_sys_font\n");
}

void reios_sys_flashrom() {
	debugf("reios_sys_flashrom\n");

	Sh4cntx.pc = Sh4cntx.pr;

	u32 cmd = Sh4cntx.r[7];
	if (cmd >= 4) {
		Sh4cntx.r[0] = -1;
		return;
	}
	switch (cmd) {
			case 0: // FLASHROM_INFO 
			{
				const u32 index = Sh4cntx.r[4];
				const u32 dest = Sh4cntx.r[5];
				//printf("reios_sys_flashrom: FLASHROM_INFO part %d dest %08x", index, dest);

				if (index < 5) {
					int offset, size;
					g_reios_ctx.flash_chip->partition_info(index,&offset,&size);
					WriteMem32(dest, offset);
					WriteMem32(dest + 4,size);
					Sh4cntx.r[0] = 0;
				} else {
					Sh4cntx.r[0] = -1;
				}
			}
			break;

			case 1:	//FLASHROM_READ 
			{
				const u32 addr = Sh4cntx.r[4];
				const u32 dest = Sh4cntx.r[5];
				const u32 size = Sh4cntx.r[6];

				//printf("FLASHROM_READ ADDR %x dest %08x size %x", addr, dest, size);
				for (u32 i = 0; i < size;++i)
					WriteMem8(dest + i,g_reios_ctx.flash_chip->Read8(addr + i));

				Sh4cntx.r[0] = size;
			}
			break;

			
			case 2:	//FLASHROM_WRITE 
			{
				const u32 dst = Sh4cntx.r[4];
				const u32 src = Sh4cntx.r[5];
				const u32 size = Sh4cntx.r[6];

				//printf("FLASHROM_WRITE offs %x src %08x size %x", dst, src, size);

				for (int i = 0; i < size; i++)
					g_reios_ctx.flash_chip->data[dst + i] &= ReadMem8(src + i);

				Sh4cntx.r[0] = size;
				//printf("FREAD %u %u %u (%c %c %c %c %c)\n", offs, src, size,pSrc[0], pSrc[1], pSrc[2], pSrc[3], pSrc[4]);
			}
			break;

			case 3:	//FLASHROM_DELETE  
			{			
				const u32 addr = Sh4cntx.r[4];

				//printf("FLASHROM_DELETE offs %x", addr);

				Sh4cntx.r[0] = -1;

				for (u32 i = 0;i < 5;++i) {
					int offs,sz;
					g_reios_ctx.flash_chip->partition_info(i,&offs,&sz);	
					if ((u32)offs == addr) {
						//printf("FLASHROM_DELETE offs %x : FOUND!!", addr);
						memset(g_reios_ctx.flash_chip->data + addr,0xff,sz);
						Sh4cntx.r[0] = 0;
						return;
					}
				}
			}
			break;
			
	default:
		printf("reios_sys_flashrom: not handled, %d r7 = 0x%x\n", cmd,Sh4cntx.r[7]);
	}
}

void reios_sys_gd() {
	gdrom_hle_op();
}
 

void reios_sys_misc() {
	printf("reios_sys_misc - r7: 0x%08X, r4 0x%08X, r5 0x%08X, r6 0x%08X\n", Sh4cntx.r[7], Sh4cntx.r[4], Sh4cntx.r[5], Sh4cntx.r[6]);

	Sh4cntx.pc = Sh4cntx.pr;
}

void reios_setup_state(u32 boot_addr) {
	/*
	Post Boot registers from actual bios boot
	r
	[0x00000000]	0xac0005d8
	[0x00000001]	0x00000009
	[0x00000002]	0xac00940c
	[0x00000003]	0x00000000
	[0x00000004]	0xac008300
	[0x00000005]	0xf4000000
	[0x00000006]	0xf4002000
	[0x00000007]	0x00000070
	[0x00000008]	0x00000000
	[0x00000009]	0x00000000
	[0x0000000a]	0x00000000
	[0x0000000b]	0x00000000
	[0x0000000c]	0x00000000
	[0x0000000d]	0x00000000
	[0x0000000e]	0x00000000
	[0x0000000f]	0x8d000000
	mac
	l	0x5bfcb024
	h	0x00000000
	r_bank
	[0x00000000]	0xdfffffff
	[0x00000001]	0x500000f1
	[0x00000002]	0x00000000
	[0x00000003]	0x00000000
	[0x00000004]	0x00000000
	[0x00000005]	0x00000000
	[0x00000006]	0x00000000
	[0x00000007]	0x00000000
	gbr	0x8c000000
	ssr	0x40000001
	spc	0x8c000776
	sgr	0x8d000000
	dbr	0x8c000010
	vbr	0x8c000000
	pr	0xac00043c
	fpul	0x00000000
	pc	0xac008300

	+		sr	{T=1 status = 0x400000f0}
	+		fpscr	{full=0x00040001}
	+		old_sr	{T=1 status=0x400000f0}
	+		old_fpscr	{full=0x00040001}

	*/

	//Setup registers to immitate a normal boot
	sh4rcb.cntx.r[15] = 0x8d000000;

	sh4rcb.cntx.gbr = 0x8c000000;
	sh4rcb.cntx.ssr = 0x40000001;
	sh4rcb.cntx.spc = 0x8c000776;
	sh4rcb.cntx.sgr = 0x8d000000;
	sh4rcb.cntx.dbr = 0x8c000010;
	sh4rcb.cntx.vbr = 0x8c000000;
	sh4rcb.cntx.pr = 0xac00043c;
	sh4rcb.cntx.fpul = 0x00000000;
	sh4rcb.cntx.pc = boot_addr;

	sh4rcb.cntx.sr.status = 0x400000f0;
	sh4rcb.cntx.sr.T = 1;

	sh4rcb.cntx.old_sr.status = 0x400000f0;

	sh4rcb.cntx.fpscr.full = 0x00040001;
	sh4rcb.cntx.old_fpscr.full = 0x00040001;

	//reg[SR] = reg[SR] & 0xefffff0f
	//sh4rcb.cntx.sr &= 0xefffff0f;
	/*
		
		src = (uint16_t *)romcopy; //0xa0....0
		dst = (uint16_t *)0x8c0000e0; //write ?
		for (i = 0; i < sizeof(romcopy); i++)
			*(dst++) = *(src++);

		//copy BootROM to RAM and continue executing at boot2(0) 
		(*(void (*)(void *, void *))0x8c0000e0)((void *)0x80000100, (void *)0x8c000100);
	*/


	//guest_to_guest_memcpy(0x8c0000e0, 0xa0000000 , 4 * 1024);
}

void reios_setuo_naomi(u32 boot_addr) {
	/*
		SR 0x60000000 0x00000001
		FPSRC 0x00040001

		-		xffr	0x13e1fe40	float [32]
		[0x0]	1.00000000	float
		[0x1]	0.000000000	float
		[0x2]	0.000000000	float
		[0x3]	0.000000000	float
		[0x4]	0.000000000	float
		[0x5]	1.00000000	float
		[0x6]	0.000000000	float
		[0x7]	0.000000000	float
		[0x8]	0.000000000	float
		[0x9]	0.000000000	float
		[0xa]	1.00000000	float
		[0xb]	0.000000000	float
		[0xc]	0.000000000	float
		[0xd]	0.000000000	float
		[0xe]	0.000000000	float
		[0xf]	1.00000000	float
		[0x10]	1.00000000	float
		[0x11]	2.14748365e+009	float
		[0x12]	0.000000000	float
		[0x13]	480.000000	float
		[0x14]	9.99999975e-006	float
		[0x15]	0.000000000	float
		[0x16]	0.00208333321	float
		[0x17]	0.000000000	float
		[0x18]	0.000000000	float
		[0x19]	2.14748365e+009	float
		[0x1a]	1.00000000	float
		[0x1b]	-1.00000000	float
		[0x1c]	0.000000000	float
		[0x1d]	0.000000000	float
		[0x1e]	0.000000000	float
		[0x1f]	0.000000000	float
		
		-		r	0x13e1fec0	unsigned int [16]
		[0x0]	0x0c021000	unsigned int
		[0x1]	0x0c01f820	unsigned int
		[0x2]	0xa0710004	unsigned int
		[0x3]	0x0c01f130	unsigned int
		[0x4]	0x5bfccd08	unsigned int
		[0x5]	0xa05f7000	unsigned int
		[0x6]	0xa05f7008	unsigned int
		[0x7]	0x00000007	unsigned int
		[0x8]	0x00000000	unsigned int
		[0x9]	0x00002000	unsigned int
		[0xa]	0xffffffff	unsigned int
		[0xb]	0x0c0e0000	unsigned int
		[0xc]	0x00000000	unsigned int
		[0xd]	0x00000000	unsigned int
		[0xe]	0x00000000	unsigned int
		[0xf]	0x0cc00000	unsigned int

		-		mac	{full=0x0000000000002000 l=0x00002000 h=0x00000000 }	Sh4Context::<unnamed-tag>::<unnamed-tag>::<unnamed-type-mac>
		full	0x0000000000002000	unsigned __int64
		l	0x00002000	unsigned int
		h	0x00000000	unsigned int
		
		-		r_bank	0x13e1ff08	unsigned int [8]
		[0x0]	0x00000000	unsigned int
		[0x1]	0x00000000	unsigned int
		[0x2]	0x00000000	unsigned int
		[0x3]	0x00000000	unsigned int
		[0x4]	0x00000000	unsigned int
		[0x5]	0x00000000	unsigned int
		[0x6]	0x00000000	unsigned int
		[0x7]	0x00000000	unsigned int
		gbr	0x0c2abcc0	unsigned int
		ssr	0x60000000	unsigned int
		spc	0x0c041738	unsigned int
		sgr	0x0cbfffb0	unsigned int
		dbr	0x00000fff	unsigned int
		vbr	0x0c000000	unsigned int
		pr	0xac0195ee	unsigned int
		fpul	0x000001e0	unsigned int
		pc	0x0c021000	unsigned int
		jdyn	0x0c021000	unsigned int

	*/

	//Setup registers to immitate a normal boot
	sh4rcb.cntx.r[0] = 0x0c021000;
	sh4rcb.cntx.r[1] = 0x0c01f820;
	sh4rcb.cntx.r[2] = 0xa0710004;
	sh4rcb.cntx.r[3] = 0x0c01f130;
	sh4rcb.cntx.r[4] = 0x5bfccd08;
	sh4rcb.cntx.r[5] = 0xa05f7000;
	sh4rcb.cntx.r[6] = 0xa05f7008;
	sh4rcb.cntx.r[7] = 0x00000007;
	sh4rcb.cntx.r[8] = 0x00000000;
	sh4rcb.cntx.r[9] = 0x00002000;
	sh4rcb.cntx.r[10] = 0xffffffff;
	sh4rcb.cntx.r[11] = 0x0c0e0000;
	sh4rcb.cntx.r[12] = 0x00000000;
	sh4rcb.cntx.r[13] = 0x00000000;
	sh4rcb.cntx.r[14] = 0x00000000;
	sh4rcb.cntx.r[15] = 0x0cc00000;

	sh4rcb.cntx.gbr = 0x0c2abcc0;
	sh4rcb.cntx.ssr = 0x60000000;
	sh4rcb.cntx.spc = 0x0c041738;
	sh4rcb.cntx.sgr = 0x0cbfffb0;
	sh4rcb.cntx.dbr = 0x00000fff;
	sh4rcb.cntx.vbr = 0x0c000000;
	sh4rcb.cntx.pr = 0xac0195ee;
	sh4rcb.cntx.fpul = 0x000001e0;
	sh4rcb.cntx.pc = boot_addr;

	sh4rcb.cntx.sr.status = 0x60000000;
	sh4rcb.cntx.sr.T = 1;

	sh4rcb.cntx.old_sr.status = 0x60000000;

	sh4rcb.cntx.fpscr.full = 0x00040001;
	sh4rcb.cntx.old_fpscr.full = 0x00040001;
}

#ifndef REIOS_WANT_EXPIREMENTAL_OLD_BUILD
void load_the_bin() {
	const char* syscalls_bin_path = "C:\\Users\\Dimitris\\Desktop\\syscall32k.bin";

	printf("Load binary...\n");
	FILE* bin = fopen(syscalls_bin_path, "rb");
	if (!bin) {
		printf("Could not find %s\n", syscalls_bin_path);
		return;
	}
	fseek(bin, 0, SEEK_END);

	size_t s = ftell(bin);
	fseek(bin, 0, SEEK_SET);
	printf("BINARY SIZE %llu\n", s);

	uint8_t* ptr = GetMemPtr(0x8c000000, s);
	if (ptr == nullptr) {
		printf("Could not bind mem \n ");
		fclose(bin);
		return;
	}
	fread((void*)ptr, s, 1, bin);
	fclose(bin);
}
#endif

void reios_boot() {
	printf("-----------------\n");
	printf("REIOS: Booting up\n");
	printf("-----------------\n");
	//setup syscalls
	//find boot file
	//boot it

	memset(GetMemPtr(0x8C000000, 0), 0xFF, 64 * 1024);

	g_reios_ctx.apply_all_hooks();
	WriteMem32(dc_bios_entrypoint_gd_do_bioscall, REIOS_OPCODE);

	//Infinitive loop for arm !
	WriteMem32(0x80800000, 0xEAFFFFFE);

	if (settings.reios.ElfFile.size()) {
		if (!reios_loadElf(settings.reios.ElfFile)) {
			msgboxf("Failed to open %s\n", MBX_ICONERROR, settings.reios.ElfFile.c_str());
		}
		reios_setup_state(0x8C010000);
	}
	else {
		if (DC_PLATFORM == DC_PLATFORM_DREAMCAST) {
			const char* bootfile = reios_locate_ip();
			if (!bootfile || !reios_locate_bootfile(bootfile))
				msgboxf("Failed to locate bootfile", MBX_ICONERROR);
#ifndef REIOS_WANT_EXPIREMENTAL_OLD_BUILD
			load_the_bin();
#endif
			g_reios_ctx.apply_all_hooks();
			reios_setup_state(0xac008300);
		}
		else {
			verify(DC_PLATFORM == DC_PLATFORM_NAOMI);
			if (CurrentCartridge == NULL)
			{
				printf("No cartridge loaded\n");
				return;
			}
			u32 data_size = 4;
			u32* sz = (u32*)CurrentCartridge->GetPtr(0x368, data_size);
			if (!sz || data_size != 4) {
				msgboxf("Naomi boot failure", MBX_ICONERROR);
			}

			int size = *sz;

			data_size = 1;
			verify(size < RAM_SIZE&& CurrentCartridge->GetPtr(size - 1, data_size) && "Invalid cart size");

			data_size = size;
			WriteMemBlock_nommu_ptr(0x0c020000, (u32*)CurrentCartridge->GetPtr(0, data_size), size);

			reios_setuo_naomi(0x0c021000);
		}
	}
}

void DYNACALL reios_trap(u32 op) {
	verify(op == REIOS_OPCODE);

	u32 pc = sh4rcb.cntx.pc - 2;
	sh4rcb.cntx.pc = sh4rcb.cntx.pr;

	u32 mapd = g_reios_ctx.syscall_addr_map(pc);

	debugf("reios: dispatch %08X -> %08X\n", pc, mapd);

	g_reios_ctx.hooks[mapd]();

}

bool reios_init(u8* rom, u8* flash,DCFlashChip* flash_chip) {
	printf("reios: Init\n");

	g_reios_ctx.biosrom = rom;
	g_reios_ctx.flashrom = flash;
	g_reios_ctx.pre_init = false;
	g_reios_ctx.flash_chip = flash_chip;

	memset(rom, 0xEA, 2048 * 1024);
	memset(GetMemPtr(0x8C000000, 0), 0, RAM_SIZE);

	u16* rom16 = (u16*)rom;

	rom16[0] = REIOS_OPCODE;

	g_reios_ctx.register_all_hooks();

	return true;
}

void reios_reset() {
	
}

void reios_term() {
	g_reios_ctx.reset();
	g_reios_ctx.pre_init = false;
}

//reios_context_t non-inl impl
void reios_context_t::remove_all_hooks() {
	this->hooks.clear();
	this->hooks_rev.clear();
}

bool reios_context_t::register_all_hooks() {
	this->remove_all_hooks();

	size_t total = 0;

	for (auto i : g_syscalls_mgr.get_map()) {
		if ((!i.second.enabled) || (i.second.fn == nullptr) )
			continue;
		
		printf("register_all_hooks::Register : %s / 0x%x @%p \n", i.first.c_str(), i.second.addr,i.second.fn);
		register_hook(i.second.addr, i.second.fn);
		++total;
	}

	printf("Registered total %llu hooks\n", total);
 
	return true;
}

bool reios_context_t::apply_all_hooks() {
	size_t total = 0;

	for (auto i : g_syscalls_mgr.get_map()) {
		if ((!i.second.enabled) || (i.second.fn == nullptr) || (i.second.syscall == k_invalid_syscall))
			continue;

		printf("apply_all_hooks:Syscall : %s / pc:0x%x sc:0x%x @%p \n", i.first.c_str(), i.second.addr, i.second.addr, i.second.fn);
		if (k_no_syscall == i.second.syscall) {
			WriteMem16(hook_addr(i.second.fn), REIOS_OPCODE);
		}
		else
			setup_syscall(hook_addr(i.second.fn), i.second.syscall);

		++total;
	}
	printf("Applied total %llu syscalls\n", total);

	return true;
}

void reios_context_t::reset() {
	gd_q.reset();
	last_cmd = 0xFFFFFFFF;	 
	dwReqID = 0xF0FFFFFF;	 
}

void reios_context_t::register_hook(u32 pc, reios_hook_fp* fn) {
	g_reios_ctx.hooks[syscall_addr_map(pc)] = fn;
	g_reios_ctx.hooks_rev[fn] = pc;
}

u32 reios_context_t::hook_addr(reios_hook_fp* fn) {
	auto it = g_reios_ctx.hooks_rev.find(fn);

	if (it != g_reios_ctx.hooks_rev.end())
		return it->second;
	else {
		printf("hook_addr: Failed to reverse lookup %08X\n", (unat)fn);
		verify(false);
		return 0;
	}
}

void reios_context_t::setup_syscall(u32 hook_addr, u32 syscall_addr) {
	WriteMem32(syscall_addr, hook_addr);
	WriteMem16(hook_addr, REIOS_OPCODE);

	debugf("reios: Patching syscall vector %08X, points to %08X\n", syscall_addr, hook_addr);
	debugf("reios: - address %08X: data %04X [%04X]\n", hook_addr, ReadMem16(hook_addr), REIOS_OPCODE);
}

void reios_context_t::sync_sys_cfg() {
	WriteMem8(0x8c000068 + 8 + 0 ,flash_chip->Read8(0x2a000000 + 0));
	WriteMem8(0x8c000068 + 8 + 1,flash_chip->Read8(0x2a000000 + 1));
	WriteMem8(0x8c000068 + 8 + 2,flash_chip->Read8(0x2a000000 + 2));
	WriteMem8(0x8c000068 + 8 + 3,flash_chip->Read8(0x2a000000 + 3));
	WriteMem8(0x8c000068 + 8 + 4,flash_chip->Read8(0x2a000000 + 4));

	flash_chip->ReadBlock(FLASH_PT_USER,FLASH_USER_SYSCFG,&fsb);

	WriteMem16(0x8c000068 + 16 + 0,fsb.time_lo);
	WriteMem16(0x8c000068 + 16 + 2,fsb.time_hi);
	WriteMem8(0x8c000068  + 16 + 4,fsb.unknown1);
	WriteMem8(0x8c000068  + 16 + 5,fsb.lang);
	WriteMem8(0x8c000068  + 16 + 6,fsb.mono);
	WriteMem8(0x8c000068  + 16 + 7,fsb.autostart);

	for (uint32_t i  = 0;i < 8; ++i)
		WriteMem8(0x8c000068 + i,flash_chip->Read8(0x2a056000 + i));

	memset(this->st,0,sizeof(this->st));
	this->st[0].u_32 = 2;
	this->st[1].u_32 = (is_gdrom) ? 0x80 : g_GDRDisc->GetDiscType();
}


void reios_context_t::identify_disc_type() {
	//1920 GD-ROM1/1
	std::string lazy;

	for (size_t i = 0;i < sizeof(reios_device_info);++i) {
		if (isalnum( reios_device_info[i]))
			lazy += std::tolower(reios_device_info[i]);
	}

	this->is_gdrom = (lazy.find("gd-rom") != std::string::npos) || (lazy.find("gdrom") != std::string::npos) ;
}

void reios_context_t::write_hle_res(const uint32_t a) {

}

void reios_context_t::write_hle_res(const uint32_t a,const uint32_t b) {

}