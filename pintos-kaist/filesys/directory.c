#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"

/* 디렉터리를 표현하는 구조체. */
struct dir {
        struct inode *inode;                /* 디렉터리가 저장된 inode. */
        off_t pos;                          /* 현재 탐색 위치. */
};

/* 디렉터리의 한 항목을 나타낸다. */
struct dir_entry {
        disk_sector_t inode_sector;         /* 해당 inode가 위치한 섹터 번호. */
        char name[NAME_MAX + 1];            /* 널 종료 문자열 형태의 파일 이름. */
        bool in_use;                        /* 사용 중이면 true. */
};

/* SECTOR 위치에 ENTRY_CNT 개의 항목을 저장할 수 있는
 * 디렉터리를 생성한다. 성공 시 true 를 반환한다. */
bool
dir_create (disk_sector_t sector, size_t entry_cnt) {
	return inode_create (sector, entry_cnt * sizeof (struct dir_entry));
}

/* INODE 를 넘겨받아 그 디렉터리를 연다.
 * 소유권을 넘겨받으며 실패 시 NULL 을 반환한다. */
struct dir *
dir_open (struct inode *inode) {
	struct dir *dir = calloc (1, sizeof *dir);
	if (inode != NULL && dir != NULL) {
		dir->inode = inode;
		dir->pos = 0;
		return dir;
	} else {
		inode_close (inode);
		free (dir);
		return NULL;
	}
}

/* 루트 디렉터리를 열어 그에 대한 dir 구조체를 반환한다. */
struct dir *
dir_open_root (void) {
	return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* DIR 와 같은 inode 를 참조하는 새 dir 구조체를 반환한다.
 * 실패하면 NULL 을 반환한다. */
struct dir *
dir_reopen (struct dir *dir) {
	return dir_open (inode_reopen (dir->inode));
}

/* DIR 을 닫고 관련된 자원을 해제한다. */
void
dir_close (struct dir *dir) {
	if (dir != NULL) {
		inode_close (dir->inode);
		free (dir);
	}
}

/* DIR 이 감싸고 있는 inode 를 반환한다. */
struct inode *
dir_get_inode (struct dir *dir) {
	return dir->inode;
}

/* DIR 에서 NAME 을 가진 파일을 찾는다.
 * 성공 시 true 를 반환하며 EP가 주어지면 엔트리를 저장하고,
 * OFSP가 주어지면 해당 엔트리의 오프셋을 돌려준다.
 * 실패하면 false 를 반환하고 EP, OFSP 는 무시된다. */
static bool
lookup (const struct dir *dir, const char *name,
		struct dir_entry *ep, off_t *ofsp) {
	struct dir_entry e;
	size_t ofs;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);

	for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
			ofs += sizeof e)
		if (e.in_use && !strcmp (name, e.name)) {
			if (ep != NULL)
				*ep = e;
			if (ofsp != NULL)
				*ofsp = ofs;
			return true;
		}
	return false;
}

/* DIR 에서 NAME 을 가진 파일을 찾아 있으면 true 를 반환한다.
 * 성공 시 *INODE 에 해당 파일의 inode 를 반환하며,
 * 실패하면 NULL 을 기록한다. 반환된 inode 는 호출자가 닫아야 한다. */
bool
dir_lookup (const struct dir *dir, const char *name,
		struct inode **inode) {
	struct dir_entry e;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);

	if (lookup (dir, name, &e, NULL))
		*inode = inode_open (e.inode_sector);
	else
		*inode = NULL;

	return *inode != NULL;
}

/* DIR 에 NAME 이라는 새 파일을 추가한다. 같은 이름이 존재해선 안 된다.
 * 파일의 inode 는 INODE_SECTOR 에 위치한다.
 * 성공 시 true 를 반환하며, NAME 이 너무 길거나
 * 디스크/메모리 오류가 발생하면 false 를 반환한다. */
bool
dir_add (struct dir *dir, const char *name, disk_sector_t inode_sector) {
	struct dir_entry e;
	off_t ofs;
	bool success = false;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);

	/* Check NAME for validity. */
	if (*name == '\0' || strlen (name) > NAME_MAX)
		return false;

	/* Check that NAME is not in use. */
	if (lookup (dir, name, NULL, NULL))
		goto done;

        /* 비어 있는 슬롯의 위치를 OFS 에 저장한다.
         * 남는 슬롯이 없으면 파일 끝 위치가 사용된다.

         * inode_read_at() 은 파일 끝에서만 짧게 읽을 수 있으므로
         * 그 외의 경우엔 메모리 부족 등 일시적 오류가 없는지 확인해야 한다. */
	for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
			ofs += sizeof e)
		if (!e.in_use)
			break;

        /* 빈 슬롯에 정보를 기록한다. */
	e.in_use = true;
	strlcpy (e.name, name, sizeof e.name);
	e.inode_sector = inode_sector;
	success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

done:
	return success;
}

/* DIR 에서 NAME 항목을 제거한다.
 * 성공하면 true, 실패하면 false 를 반환한다.
 * 실패 조건은 해당 이름의 파일이 존재하지 않을 때뿐이다. */
bool
dir_remove (struct dir *dir, const char *name) {
	struct dir_entry e;
	struct inode *inode = NULL;
	bool success = false;
	off_t ofs;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);

        /* 디렉터리 엔트리를 찾는다. */
	if (!lookup (dir, name, &e, &ofs))
		goto done;

        /* 해당 inode 를 연다. */
	inode = inode_open (e.inode_sector);
	if (inode == NULL)
		goto done;

        /* 디렉터리 엔트리를 지운다. */
	e.in_use = false;
	if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e)
		goto done;

        /* 실제 inode 도 삭제한다. */
	inode_remove (inode);
	success = true;

done:
	inode_close (inode);
	return success;
}

/* DIR 에서 다음 항목의 이름을 NAME 에 저장한다.
 * 더 읽을 엔트리가 없으면 false 를 반환한다. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1]) {
	struct dir_entry e;

	while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) {
		dir->pos += sizeof e;
		if (e.in_use) {
			strlcpy (name, e.name, NAME_MAX + 1);
			return true;
		}
	}
	return false;
}