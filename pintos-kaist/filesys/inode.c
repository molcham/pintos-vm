#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* inode를 식별하는 값. */
#define INODE_MAGIC 0x494e4f44

/* 디스크에 저장되는 inode 구조체.
 * 크기는 정확히 DISK_SECTOR_SIZE 바이트여야 한다. */
struct inode_disk {
        disk_sector_t start;                /* 첫 데이터 섹터. */
        off_t length;                       /* 파일 크기(바이트). */
        unsigned magic;                     /* 매직 넘버. */
        uint32_t unused[125];               /* 사용하지 않음. */
};

/* SIZE 바이트 파일을 저장하기 위해 필요한 섹터 수를 반환한다. */
static inline size_t
bytes_to_sectors (off_t size) {
	return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
}

/* 메모리에 존재하는 inode. */
struct inode {
        struct list_elem elem;              /* inode 목록에 들어갈 요소. */
        disk_sector_t sector;               /* 디스크상의 섹터 번호. */
        int open_cnt;                       /* 열린 횟수. */
        bool removed;                       /* 삭제된 경우 true. */
        int deny_write_cnt;                 /* 0 이면 쓰기 허용, >0 이면 금지. */
        struct inode_disk data;             /* inode 내용. */
};

/* INODE 의 POS 오프셋이 속한 디스크 섹터를 반환한다.
 * 해당 위치가 없으면 -1 을 반환한다. */
static disk_sector_t
byte_to_sector (const struct inode *inode, off_t pos) {
	ASSERT (inode != NULL);
	if (pos < inode->data.length)
		return inode->data.start + pos / DISK_SECTOR_SIZE;
	else
		return -1;
}

/* 이미 열린 inode 를 중복해서 열면 같은 구조체를 사용하도록 하는 목록. */
static struct list open_inodes;

/* inode 모듈 초기화. */
void
inode_init (void) {
	list_init (&open_inodes);
}

/* LENGTH 바이트 크기로 inode 를 초기화하여 SECTOR 섹터에 기록한다.
 * 성공 시 true, 메모리나 디스크 할당 실패 시 false 를 반환한다. */
bool
inode_create (disk_sector_t sector, off_t length) {
	struct inode_disk *disk_inode = NULL;
	bool success = false;

	ASSERT (length >= 0);

	/* If this assertion fails, the inode structure is not exactly
	 * one sector in size, and you should fix that. */
	ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

	disk_inode = calloc (1, sizeof *disk_inode);
	if (disk_inode != NULL) {
		size_t sectors = bytes_to_sectors (length);
		disk_inode->length = length;
		disk_inode->magic = INODE_MAGIC;
		if (free_map_allocate (sectors, &disk_inode->start)) {
			disk_write (filesys_disk, sector, disk_inode);
			if (sectors > 0) {
				static char zeros[DISK_SECTOR_SIZE];
				size_t i;

				for (i = 0; i < sectors; i++) 
					disk_write (filesys_disk, disk_inode->start + i, zeros); 
			}
			success = true; 
		} 
		free (disk_inode);
	}
	return success;
}

/* SECTOR 위치에서 inode 를 읽어와 구조체를 반환한다.
 * 메모리 할당에 실패하면 NULL 을 반환한다. */
struct inode *
inode_open (disk_sector_t sector) {
	struct list_elem *e;
	struct inode *inode;

        /* 이미 열려 있는 inode 인지 확인한다. */
	for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
			e = list_next (e)) {
		inode = list_entry (e, struct inode, elem);
		if (inode->sector == sector) {
			inode_reopen (inode);
			return inode; 
		}
	}

        /* 메모리 할당. */
	inode = malloc (sizeof *inode);
	if (inode == NULL)
		return NULL;

        /* 초기화. */
	list_push_front (&open_inodes, &inode->elem);
	inode->sector = sector;
	inode->open_cnt = 1;
	inode->deny_write_cnt = 0;
	inode->removed = false;
	disk_read (filesys_disk, inode->sector, &inode->data);
	return inode;
}

/* INODE 를 다시 열어 반환한다. */
struct inode *
inode_reopen (struct inode *inode) {
	if (inode != NULL)
		inode->open_cnt++;
	return inode;
}

/* INODE 의 번호를 반환한다. */
disk_sector_t
inode_get_inumber (const struct inode *inode) {
	return inode->sector;
}

/* INODE 를 닫고 디스크에 기록한다.
 * 마지막 참조라면 메모리를 해제하고, 삭제 표기된 경우 블록도 해제한다. */
void
inode_close (struct inode *inode) {
        /* NULL 포인터는 무시. */
	if (inode == NULL)
		return;

        /* 마지막으로 열려 있던 경우 자원을 해제한다. */
	if (--inode->open_cnt == 0) {
                /* 목록에서 제거하고 잠금을 해제한다. */
		list_remove (&inode->elem);

                /* 삭제 표시된 경우 블록을 반환한다. */
		if (inode->removed) {
			free_map_release (inode->sector, 1);
			free_map_release (inode->data.start,
					bytes_to_sectors (inode->data.length)); 
		}

		free (inode); 
	}
}

/* 마지막으로 닫힐 때 삭제되도록 INODE 에 표시한다. */
void
inode_remove (struct inode *inode) {
	ASSERT (inode != NULL);
	inode->removed = true;
}

/* OFFSET 위치부터 SIZE 바이트를 INODE 에서 BUFFER 로 읽어 온다.
 * 실제로 읽은 바이트 수를 반환하며, 오류나 EOF 로 더 적을 수 있다. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) {
	uint8_t *buffer = buffer_;
	off_t bytes_read = 0;
	uint8_t *bounce = NULL;

	while (size > 0) {
                /* 읽어 올 섹터 번호와 섹터 내 오프셋 계산. */
		disk_sector_t sector_idx = byte_to_sector (inode, offset);
		int sector_ofs = offset % DISK_SECTOR_SIZE;

                /* inode 와 섹터에서 남은 바이트 수 중 더 작은 값. */
		off_t inode_left = inode_length (inode) - offset;
		int sector_left = DISK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

                /* 이번에 복사할 바이트 수. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) {
                        /* 한 섹터 전체를 바로 버퍼로 읽는다. */
			disk_read (filesys_disk, sector_idx, buffer + bytes_read); 
		} else {
                        /* 섹터를 bounce 버퍼에 읽어 부분만 호출자 버퍼로 복사. */
			if (bounce == NULL) {
				bounce = malloc (DISK_SECTOR_SIZE);
				if (bounce == NULL)
					break;
			}
			disk_read (filesys_disk, sector_idx, bounce);
			memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
		}

                /* 진행. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_read += chunk_size;
	}
	free (bounce);

	return bytes_read;
}

/* OFFSET 위치부터 SIZE 바이트를 INODE 에 기록한다.
 * 실제 기록된 바이트 수를 반환하며, EOF 나 오류로 적을 수 있다.
 * 보통은 파일 끝에서 쓰면 inode 를 확장해야 하지만 아직 구현되지 않았다. */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
		off_t offset) {
	const uint8_t *buffer = buffer_;
	off_t bytes_written = 0;
	uint8_t *bounce = NULL;

	if (inode->deny_write_cnt)
		return 0;

	while (size > 0) {
                /* 기록할 섹터 번호와 섹터 내 시작 위치. */
		disk_sector_t sector_idx = byte_to_sector (inode, offset);
		int sector_ofs = offset % DISK_SECTOR_SIZE;

                /* inode 에 남은 바이트와 섹터에 남은 바이트 중 더 작은 값. */
		off_t inode_left = inode_length (inode) - offset;
		int sector_left = DISK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

                /* 이번에 이 섹터에 쓸 바이트 수. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) {
                        /* 한 섹터 전체를 바로 디스크에 쓴다. */
			disk_write (filesys_disk, sector_idx, buffer + bytes_written); 
		} else {
                        /* bounce 버퍼가 필요하다. */
			if (bounce == NULL) {
				bounce = malloc (DISK_SECTOR_SIZE);
				if (bounce == NULL)
					break;
			}

                        /* 섹터의 일부만 덮어쓸 경우 미리 읽어 와야 하며
                           그렇지 않으면 0으로 초기화된 버퍼를 사용한다. */
			if (sector_ofs > 0 || chunk_size < sector_left) 
				disk_read (filesys_disk, sector_idx, bounce);
			else
				memset (bounce, 0, DISK_SECTOR_SIZE);
			memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
			disk_write (filesys_disk, sector_idx, bounce); 
		}

                /* 진행. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_written += chunk_size;
	}
	free (bounce);

	return bytes_written;
}

/* INODE 에 대한 쓰기를 금지한다.
   한 번만 호출할 수 있다. */
	void
inode_deny_write (struct inode *inode) 
{
	inode->deny_write_cnt++;
	ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* inode_deny_write() 를 호출했던 스레드는 닫기 전에
 * 반드시 이 함수를 호출해 쓰기를 다시 허용해야 한다. */
void
inode_allow_write (struct inode *inode) {
	ASSERT (inode->deny_write_cnt > 0);
	ASSERT (inode->deny_write_cnt <= inode->open_cnt);
	inode->deny_write_cnt--;
}

/* INODE 데이터의 크기(바이트)를 반환한다. */
off_t
inode_length (const struct inode *inode) {
	return inode->data.length;
}