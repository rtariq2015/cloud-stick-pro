/* Read-only compatibility check against the actual firmware SDK's lfs.c.
 * cc -std=c99 -I SDK/littlefs tools/verify_empty_journal.c SDK/littlefs/lfs.c
 *    SDK/littlefs/lfs_util.c -o verify-journal
 * verify-journal empty-journal.bin
 */
#include <stdio.h>
#include <string.h>
#include "lfs.h"
static unsigned char image[0x100000];
static int rd(const struct lfs_config *c, lfs_block_t b, lfs_off_t o, void *p, lfs_size_t n) {
    (void)c;
    if ((size_t)b * 4096 + o + n > sizeof(image)) return LFS_ERR_IO;
    memcpy(p, image + b * 4096 + o, n); return 0;
}
static int no_program(const struct lfs_config *c, lfs_block_t b, lfs_off_t o, const void *p, lfs_size_t n) {
    (void)c;(void)b;(void)o;(void)p;(void)n; return LFS_ERR_IO;
}
static int no_erase(const struct lfs_config *c,lfs_block_t b) {(void)c;(void)b;return LFS_ERR_IO;}
static int sync_image(const struct lfs_config *c) {(void)c;return 0;}
int main(int argc,char **argv) {
    if(argc!=2) return 1;
    FILE *f=fopen(argv[1],"rb"); if(!f) return 2;
    if(fread(image,1,sizeof(image),f)!=sizeof(image) || fgetc(f)!=EOF) return 3;
    fclose(f);
    struct lfs_config cfg={.read=rd,.prog=no_program,.erase=no_erase,.sync=sync_image,
       .read_size=128,.prog_size=128,.block_size=4096,.block_count=256,.cache_size=512,
       .lookahead_size=128,.block_cycles=512};
    lfs_t fs; lfs_dir_t dir; struct lfs_info info; int count=0,rc;
    if(lfs_mount(&fs,&cfg)!=0 || lfs_dir_open(&fs,&dir,"/")!=0) return 4;
    while((rc=lfs_dir_read(&fs,&dir,&info))>0) {
        if(!strcmp(info.name,".") || !strcmp(info.name,"..")) continue;
        if(strcmp(info.name,"queue") || info.type!=LFS_TYPE_DIR) return 5;
        count++;
    }
    if(rc<0 || count!=1 || lfs_dir_close(&fs,&dir)!=0 || lfs_dir_open(&fs,&dir,"/queue")!=0) return 6;
    while((rc=lfs_dir_read(&fs,&dir,&info))>0) {
        if(strcmp(info.name,".") && strcmp(info.name,"..")) return 7;
    }
    if(rc<0 || lfs_dir_close(&fs,&dir)!=0 || lfs_unmount(&fs)!=0) return 8;
    puts("Pinned SDK LittleFS mounted image read-only; exactly one empty /queue directory"); return 0;
}
