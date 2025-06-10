#ifndef __LIB_KERNEL_HASH_H
#define __LIB_KERNEL_HASH_H

/* Hash table.
 *
 * 이 자료구조는 Pintos 프로젝트 3을 위한 Tour of Pintos 문서에서
 * 자세히 다루고 있습니다.
 *
 * 여기서 사용되는 방식은 체이닝(chaining) 기법을 이용한 표준 해시 테이블입니다.
 * 테이블에서 요소를 찾을 때는 먼저 해당 데이터에 대해 해시 함수를 계산하여
 * 그 값을 버킷 배열의 인덱스로 사용합니다. 각 버킷은 이중 연결 리스트이며,
 * 필요 시 리스트를 순차적으로 탐색하여 원하는 항목을 찾습니다.
 *
 * 버킷 내의 리스트는 동적으로 할당하지 않습니다.
 * 해시 테이블에 들어갈 수 있는 구조체는 모두 내부에 struct hash_elem
 * 멤버를 포함해야 합니다. 모든 해시 관련 연산은 이 hash_elem을 기준으로 이루어지며,
 * hash_entry 매크로를 이용하면 hash_elem을 포함한 원래 구조체로
 * 손쉽게 변환할 수 있습니다. 이는 lib/kernel/list.h에 구현된
 * 연결 리스트와 동일한 방식입니다. 해당 파일의 주석을 참고하면
 * 더 자세한 설명을 볼 수 있습니다.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "list.h"
#include "threads/thread.h"

/* Hash element. */
struct hash_elem {
	struct list_elem list_elem;
};

/* HASH_ELEM 포인터를, 해당 요소를 포함하고 있는 구조체로 변환합니다.
 * 외부 구조체의 이름(STRUCT)과 hash_elem 멤버의 이름(MEMBER)을 인자로 주면 되며,
 * 위에 있는 큰 주석의 예시를 참고하면 사용법을 이해하는 데 도움이 됩니다. */
#define hash_entry(HASH_ELEM, STRUCT, MEMBER)                   \
	((STRUCT *) ((uint8_t *) &(HASH_ELEM)->list_elem        \
		- offsetof (STRUCT, MEMBER.list_elem)))

/* 해시 엔트리 e와 추가 데이터 AUX를 이용해 해시 값을 계산하는 함수의 형태입니다. */
typedef uint64_t hash_hash_func (const struct hash_elem *e, void *aux);

/* 두 해시 엔트리 A, B를 비교할 때 사용하는 함수입니다.
 * 필요한 경우 AUX로 보조 데이터를 넘겨 줄 수 있습니다.
 * A가 B보다 작으면 true, 그렇지 않으면 false를 반환합니다. */
typedef bool hash_less_func (const struct hash_elem *a,
		const struct hash_elem *b,
		void *aux);

/* 특정 해시 엔트리에 대해 어떤 작업을 수행할 때 사용하는 함수 형태입니다.
 * AUX에 부가 정보를 전달할 수 있습니다. */
typedef void hash_action_func (struct hash_elem *e, void *aux);

/* Hash table. */
struct hash {
	size_t elem_cnt;            /* 테이블에 들어 있는 요소의 개수 */
	size_t bucket_cnt;          /* 버킷의 개수 (항상 2의 제곱수) */
	struct list *buckets;       /* 각 버킷을 나타내는 연결 리스트 배열 */
	hash_hash_func *hash;       /* 해시 함수 */
	hash_less_func *less;       /* 요소 비교 함수 */
	void *aux;                  /* 해시와 비교 함수에서 사용할 추가 데이터 */
};

/* 해시 테이블을 순회하기 위한 구조체입니다. */
struct hash_iterator {
	struct hash *hash;          /* 현재 탐색 중인 해시 테이블 */
	struct list *bucket;        /* 현재 가리키는 버킷 */
	struct hash_elem *elem;     /* 현재 버킷 안에서의 위치 */
};

/* 해시 테이블의 초기화, 비우기, 파괴 관련 함수들입니다. */
bool hash_init (struct hash *, hash_hash_func *, hash_less_func *, void *aux);
void hash_clear (struct hash *, hash_action_func *);
void hash_destroy (struct hash *, hash_action_func *);

/* 요소를 삽입하고 교체하며, 검색하거나 삭제할 때 사용하는 함수들입니다. */
struct hash_elem *hash_insert (struct hash *, struct hash_elem *);
struct hash_elem *hash_replace (struct hash *, struct hash_elem *);
struct hash_elem *hash_find (struct hash *, struct hash_elem *);
struct hash_elem *hash_delete (struct hash *, struct hash_elem *);

/* 해시 테이블을 순회할 때 사용되는 함수들로, 먼저 hash_first()로
 * 순회를 시작하고 hash_next()로 다음 요소를 얻습니다.
 * 현재 위치의 요소는 hash_cur()로 접근합니다. */
void hash_apply (struct hash *, hash_action_func *);
void hash_first (struct hash_iterator *, struct hash *);
struct hash_elem *hash_next (struct hash_iterator *);
struct hash_elem *hash_cur (struct hash_iterator *);

/* 해시 테이블의 크기나 비어 있는지 여부 등을 확인하는 함수들입니다. */
size_t hash_size (struct hash *);
bool hash_empty (struct hash *);

/* 간단히 활용할 수 있는 해시 함수 예제들로, 바이트 배열, 문자열,
 *정수 등을 해시 값으로 변환합니다. */
uint64_t hash_bytes (const void *, size_t);
uint64_t hash_string (const char *);
uint64_t hash_int (int);

#endif /* lib/kernel/hash.h */
