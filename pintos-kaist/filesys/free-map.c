#include "filesys/free-map.h"
#include <bitmap.h>
#include <debug.h>
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"

static struct file *free_map_file;   /* 프리 맵을 보관하는 파일. */
static struct bitmap *free_map;      /* 디스크 섹터당 비트 하나로 구성된 프리 맵. */

/* 프리 맵을 초기화한다. */
void
free_map_init (void) {
	free_map = bitmap_create (disk_size (filesys_disk));
	if (free_map == NULL)
		PANIC ("bitmap creation failed--disk is too large");
	bitmap_mark (free_map, FREE_MAP_SECTOR);
	bitmap_mark (free_map, ROOT_DIR_SECTOR);
}

/* 프리 맵에서 CNT 개의 연속된 섹터를 할당하고 첫 번째 번호를 *SECTORP 에 저장한다.
 * 성공 시 true, 할당할 섹터가 없으면 false 를 반환한다. */
bool
free_map_allocate (size_t cnt, disk_sector_t *sectorp) {
	disk_sector_t sector = bitmap_scan_and_flip (free_map, 0, cnt, false);
	if (sector != BITMAP_ERROR
			&& free_map_file != NULL
			&& !bitmap_write (free_map, free_map_file)) {
		bitmap_set_multiple (free_map, sector, cnt, false);
		sector = BITMAP_ERROR;
	}
	if (sector != BITMAP_ERROR)
		*sectorp = sector;
	return sector != BITMAP_ERROR;
}

/* SECTOR 부터 CNT 개의 섹터를 다시 사용할 수 있도록 표시한다. */
void
free_map_release (disk_sector_t sector, size_t cnt) {
	ASSERT (bitmap_all (free_map, sector, cnt));
	bitmap_set_multiple (free_map, sector, cnt, false);
	bitmap_write (free_map, free_map_file);
}

/* 프리 맵 파일을 열어 내용을 메모리로 읽어 온다. */
void
free_map_open (void) {
	free_map_file = file_open (inode_open (FREE_MAP_SECTOR));
	if (free_map_file == NULL)
		PANIC ("can't open free map");
	if (!bitmap_read (free_map, free_map_file))
		PANIC ("can't read free map");
}

/* 프리 맵을 디스크에 기록하고 파일을 닫는다. */
void
free_map_close (void) {
	file_close (free_map_file);
}

/* 새 프리 맵 파일을 만들고 현재 프리 맵을 기록한다. */
void
free_map_create (void) {
        /* inode 생성 */
	if (!inode_create (FREE_MAP_SECTOR, bitmap_file_size (free_map)))
		PANIC ("free map creation failed");

        /* 비트맵을 파일에 기록 */
	free_map_file = file_open (inode_open (FREE_MAP_SECTOR));
	if (free_map_file == NULL)
		PANIC ("can't open free map");
	if (!bitmap_write (free_map, free_map_file))
		PANIC ("can't write free map");
}
