#include "bitmap.h"
#include <debug.h>
#include <limits.h>
#include <round.h>
#include <stdio.h>
#include "threads/malloc.h"
#ifdef FILESYS
#include "filesys/file.h"
#endif

/* 요소 타입.
   최소한 int 만큼의 크기를 갖는 부호 없는 정수형이어야 합니다.
   이 타입의 각 비트는 비트맵의 한 비트를 의미합니다.
   예를 들어 한 요소의 비트 0이 비트맵의 K번째 비트라면
   비트 1은 K+1번째 비트를 나타내는 식으로 순차적으로 대응됩니다. */
typedef unsigned long elem_type;

/* 한 요소가 담고 있는 비트 수 */
#define ELEM_BITS (sizeof (elem_type) * CHAR_BIT)

/* 한국어 번역:
   외부에서 보면 비트맵은 단순한 비트들의 배열이지만,
   내부 구현은 위에서 정의한 elem_type 배열로 이루어져
   비트 배열을 흉내 낸 구조입니다. */
struct bitmap {
	size_t bit_cnt;     /* 비트맵이 표현하는 전체 비트 수 */
	elem_type *bits;    /* 실제 비트를 담고 있는 요소들의 배열 */
};

/* BIT_IDX 번 비트를 담고 있는 요소의 인덱스를 반환 */
static inline size_t
elem_idx (size_t bit_idx) {
	return bit_idx / ELEM_BITS;
}

/* BIT_IDX 비트에 해당하는 위치만 켜진 elem_type 값을 반환 */
static inline elem_type
bit_mask (size_t bit_idx) {
	return (elem_type) 1 << (bit_idx % ELEM_BITS);
}

/* BIT_CNT 비트를 표현하기 위해 필요한 요소 개수를 반환 */
static inline size_t
elem_cnt (size_t bit_cnt) {
	return DIV_ROUND_UP (bit_cnt, ELEM_BITS);
}

/* BIT_CNT 비트를 저장하기 위해 필요한 바이트 수를 반환 */
static inline size_t
byte_cnt (size_t bit_cnt) {
	return sizeof (elem_type) * elem_cnt (bit_cnt);
}

/* 비트맵 마지막 요소에서 실제로 사용되는 비트는 1로, 
   남는 비트는 0으로 설정한 비트 마스크를 반환 */
static inline elem_type
last_mask (const struct bitmap *b) {
	int last_bits = b->bit_cnt % ELEM_BITS;
	return last_bits ? ((elem_type) 1 << last_bits) - 1 : (elem_type) -1;
}

/* 비트맵을 생성하고 파괴하는 함수들 */

/* BIT_CNT 크기의 비트맵 B를 초기화하고 모든 비트를 0으로 설정합니다.
   성공하면 비트맵을 반환하고, 메모리 할당 실패 시 NULL을 반환합니다. */
struct bitmap *
bitmap_create (size_t bit_cnt) {
	struct bitmap *b = malloc (sizeof *b);
	if (b != NULL) {
		b->bit_cnt = bit_cnt;
		b->bits = malloc (byte_cnt (bit_cnt));
		if (b->bits != NULL || bit_cnt == 0) {
			bitmap_set_all (b, false);
			return b;
		}
		free (b);
	}
	return NULL;
}

/* BLOCK에 미리 할당된 BLOCK_SIZE 바이트 공간을 이용하여
   BIT_CNT 비트를 담는 비트맵을 생성하여 반환합니다.
   BLOCK_SIZE는 최소한 bitmap_needed_bytes(BIT_CNT)보다 커야 합니다. */
struct bitmap *
bitmap_create_in_buf (size_t bit_cnt, void *block, size_t block_size UNUSED) {
	struct bitmap *b = block;

	ASSERT (block_size >= bitmap_buf_size (bit_cnt));

	b->bit_cnt = bit_cnt;
	b->bits = (elem_type *) (b + 1);
	bitmap_set_all (b, false);
	return b;
}

/* bitmap_create_in_buf() 사용 시 BIT_CNT 비트를 담기 위해
   필요한 전체 바이트 수를 계산하여 반환 */
size_t
bitmap_buf_size (size_t bit_cnt) {
	return sizeof (struct bitmap) + byte_cnt (bit_cnt);
}

/* bitmap_create()로 생성한 비트맵 B의 메모리를 해제합니다.
   bitmap_create_preallocated()로 생성한 비트맵에는 사용하지 않습니다. */
void
bitmap_destroy (struct bitmap *b) {
	if (b != NULL) {
		free (b->bits);
		free (b);
	}
}

/* 비트맵의 전체 크기에 관한 함수 */

/* 비트맵 B가 가지고 있는 비트 수를 반환 */
size_t
bitmap_size (const struct bitmap *b) {
	return b->bit_cnt;
}

/* 하나의 비트를 설정하거나 확인하는 함수들 */

/* IDX 번째 비트를 VALUE 값으로 원자적으로 변경 */
void
bitmap_set (struct bitmap *b, size_t idx, bool value) {
	ASSERT (b != NULL);
	ASSERT (idx < b->bit_cnt);
	if (value)
		bitmap_mark (b, idx);
	else
		bitmap_reset (b, idx);
}

/* BIT_IDX 번째 비트를 원자적으로 1로 설정 */
void
bitmap_mark (struct bitmap *b, size_t bit_idx) {
	size_t idx = elem_idx (bit_idx);
	elem_type mask = bit_mask (bit_idx);

	/* `b->bits[idx] |= mask` 와 같지만 단일 프로세서 환경에서
		원자성을 보장하기 위해 어셈블리 명령을 사용합니다.
		자세한 내용은 [IA32-v2b]의 OR 명령 설명을 참고하세요. */
	asm ("lock orq %1, %0" : "=m" (b->bits[idx]) : "r" (mask) : "cc");
}

/* BIT_IDX 번째 비트를 원자적으로 0으로 설정 */
void
bitmap_reset (struct bitmap *b, size_t bit_idx) {
	size_t idx = elem_idx (bit_idx);
	elem_type mask = bit_mask (bit_idx);

	/* `b->bits[idx] &= ~mask` 와 같은 동작을 하지만 단일 프로세서에서
		원자성을 위해 어셈블리 AND 명령을 사용합니다. [IA32-v2a] 참고 */
	asm ("lock andq %1, %0" : "=m" (b->bits[idx]) : "r" (~mask) : "cc");
}

/* IDX 번째 비트가 1이면 0으로, 0이면 1로 원자적으로 뒤집습니다. */
void
bitmap_flip (struct bitmap *b, size_t bit_idx) {
	size_t idx = elem_idx (bit_idx);
	elem_type mask = bit_mask (bit_idx);

	/* `b->bits[idx] ^= mask` 와 같은 역할을 하지만 단일 프로세서에서
		원자성을 보장하기 위해 XOR 명령을 사용합니다. */
	asm ("lock xorq %1, %0" : "=m" (b->bits[idx]) : "r" (mask) : "cc");
}

/* 비트맵 B에서 IDX 번째 비트의 값을 반환 */
bool
bitmap_test (const struct bitmap *b, size_t idx) {
	ASSERT (b != NULL);
	ASSERT (idx < b->bit_cnt);
	return (b->bits[elem_idx (idx)] & bit_mask (idx)) != 0;
}

/* 여러 비트를 한꺼번에 설정하거나 검사 */

/* 비트맵 B의 모든 비트를 VALUE 값으로 채웁니다. */
void
bitmap_set_all (struct bitmap *b, bool value) {
	ASSERT (b != NULL);

	bitmap_set_multiple (b, 0, bitmap_size (b), value);
}

/* START 위치부터 CNT개의 비트를 VALUE로 설정 */
void
bitmap_set_multiple (struct bitmap *b, size_t start, size_t cnt, bool value) {
	size_t i;

	ASSERT (b != NULL);
	ASSERT (start <= b->bit_cnt);
	ASSERT (start + cnt <= b->bit_cnt);

	for (i = 0; i < cnt; i++)
		bitmap_set (b, start + i, value);
}

/* START부터 CNT개의 비트 중 VALUE와 같은 값의 비트 개수를 반환 */
size_t
bitmap_count (const struct bitmap *b, size_t start, size_t cnt, bool value) {
	size_t i, value_cnt;

	ASSERT (b != NULL);
	ASSERT (start <= b->bit_cnt);
	ASSERT (start + cnt <= b->bit_cnt);

	value_cnt = 0;
	for (i = 0; i < cnt; i++)
		if (bitmap_test (b, start + i) == value)
			value_cnt++;
	return value_cnt;
}

/* 구간 [START, START+CNT) 중 VALUE 값인 비트가 하나라도 있으면 true 반환 */
bool
bitmap_contains (const struct bitmap *b, size_t start, size_t cnt, bool value) {
	size_t i;

	ASSERT (b != NULL);
	ASSERT (start <= b->bit_cnt);
	ASSERT (start + cnt <= b->bit_cnt);

	for (i = 0; i < cnt; i++)
		if (bitmap_test (b, start + i) == value)
			return true;
	return false;
}

/* 구간 내에 1로 설정된 비트가 하나라도 있으면 true */
bool
bitmap_any (const struct bitmap *b, size_t start, size_t cnt) {
	return bitmap_contains (b, start, cnt, true);
}

/* 구간 내에 1로 설정된 비트가 전혀 없으면 true */
bool
bitmap_none (const struct bitmap *b, size_t start, size_t cnt) {
	return !bitmap_contains (b, start, cnt, true);
}

/* 구간의 모든 비트가 1이면 true, 아니면 false */
bool
bitmap_all (const struct bitmap *b, size_t start, size_t cnt) {
	return !bitmap_contains (b, start, cnt, false);
}

/* 특정 값의 연속된 비트를 찾는 함수들 */

/* START 위치 이후에서 VALUE 값으로 연속된 CNT개의 비트를 찾아
   그 시작 인덱스를 반환합니다. 찾지 못하면 BITMAP_ERROR 반환 */
size_t
bitmap_scan (const struct bitmap *b, size_t start, size_t cnt, bool value) {
	ASSERT (b != NULL);
	ASSERT (start <= b->bit_cnt);

	if (cnt <= b->bit_cnt) {
		size_t last = b->bit_cnt - cnt;
		size_t i;
		for (i = start; i <= last; i++)
			if (!bitmap_contains (b, i, cnt, !value))
				return i;
	}
	return BITMAP_ERROR;
}

/* START 이후에서 VALUE 값으로 CNT개 연속된 비트를 찾아 모두 반전시키고
   그 첫 번째 비트의 인덱스를 반환합니다.
   없으면 BITMAP_ERROR를, CNT가 0이면 0을 반환합니다.
   비트 변경은 원자적으로 이루어지지만 검사 과정은 원자적이지 않습니다. */
size_t
bitmap_scan_and_flip (struct bitmap *b, size_t start, size_t cnt, bool value) {
	size_t idx = bitmap_scan (b, start, cnt, value);
	if (idx != BITMAP_ERROR)
		bitmap_set_multiple (b, idx, cnt, !value);
	return idx;
}

/* 파일과 비트맵 간의 입출력 관련 함수 */

#ifdef FILESYS
/* 비트맵 B를 파일로 저장하는 데 필요한 바이트 수 반환 */
size_t
bitmap_file_size (const struct bitmap *b) {
	return byte_cnt (b->bit_cnt);
}

/* FILE에서 비트맵 B를 읽어오며 성공 여부를 반환 */
bool
bitmap_read (struct bitmap *b, struct file *file) {
	bool success = true;
	if (b->bit_cnt > 0) {
		off_t size = byte_cnt (b->bit_cnt);
		success = file_read_at (file, b->bits, size, 0) == size;
		b->bits[elem_cnt (b->bit_cnt) - 1] &= last_mask (b);
	}
	return success;
}

/* 비트맵 B를 FILE에 기록하고 성공 여부를 반환 */
bool
bitmap_write (const struct bitmap *b, struct file *file) {
	off_t size = byte_cnt (b->bit_cnt);
	return file_write_at (file, b->bits, size, 0) == size;
}
#endif /* FILESYS */

/* 디버깅을 위한 보조 함수 */

/* 비트맵 B의 내용을 16진수 형태로 콘솔에 출력 */
void
bitmap_dump (const struct bitmap *b) {
	hex_dump (0, b->bits, byte_cnt (b->bit_cnt), false);
}

