/* 해시 테이블.

   이 자료구조에 대한 자세한 설명은 Pintos 프로젝트 3의
   "Tour of Pintos" 문서에 잘 나와 있습니다.

   기본적인 정보는 hash.h 파일을 참고하세요. */

#include "hash.h"
#include "../debug.h"
#include "threads/malloc.h"

#define list_elem_to_hash_elem(LIST_ELEM)                       \
	list_entry(LIST_ELEM, struct hash_elem, list_elem)

static struct list *find_bucket (struct hash *, struct hash_elem *);
static struct hash_elem *find_elem (struct hash *, struct list *,
		struct hash_elem *);
static void insert_elem (struct hash *, struct list *, struct hash_elem *);
static void remove_elem (struct hash *, struct hash_elem *);
static void rehash (struct hash *);

/* HASH 함수를 사용해 해시 값을 계산하고
   LESS 함수를 이용해 항목을 비교하도록 H 해시 테이블을 초기화합니다.
   AUX 인자는 부가적인 데이터를 전달할 때 사용됩니다. */
bool
hash_init (struct hash *h, hash_hash_func *hash, hash_less_func *less, void *aux) {
	h->elem_cnt = 0;
	h->bucket_cnt = 4;
	h->buckets = malloc (sizeof *h->buckets * h->bucket_cnt);
	h->hash = hash;
	h->less = less;
	h->aux = aux;

	if (h->buckets != NULL) {
		hash_clear (h, NULL);
		return true;
	} else
		return false;
}

/* H에 들어 있는 모든 요소를 제거합니다.

   DESTRUCTOR가 NULL이 아니면 해시의 각 요소에 대해 호출됩니다.
   필요하다면 DESTRUCTOR에서 해당 요소가 점유한 메모리를 해제할 수 있습니다.
   하지만 hash_clear()가 실행되는 동안 hash_clear(), hash_destroy(),
   hash_insert(), hash_replace(), hash_delete() 중 하나라도 호출하여
   해시 테이블 H를 수정하면( DESTRUCTOR 안이든 다른 곳이든 )
   동작이 정의되어 있지 않습니다. */
void
hash_clear (struct hash *h, hash_action_func *destructor) {
	size_t i;

	for (i = 0; i < h->bucket_cnt; i++) {
		struct list *bucket = &h->buckets[i];

		if (destructor != NULL)
			while (!list_empty (bucket)) {
				struct list_elem *list_elem = list_pop_front (bucket);
				struct hash_elem *hash_elem = list_elem_to_hash_elem (list_elem);
				destructor (hash_elem, h->aux);
			}

		list_init (bucket);
	}

	h->elem_cnt = 0;
}

/* 해시 테이블 H를 파괴합니다.

   DESTRUCTOR가 NULL이 아니면 해시에 있는 각 요소에 대해 먼저 호출됩니다.
   필요하다면 이 함수에서 해당 요소가 사용한 메모리를 해제할 수 있습니다.
   하지만 hash_clear()가 실행되는 동안 hash_clear(), hash_destroy(),
   hash_insert(), hash_replace(), hash_delete() 중 하나라도 호출하여
   해시 테이블 H를 수정하면( DESTRUCTOR 안이든 다른 곳이든 )
   동작이 정의되어 있지 않습니다. */
void
hash_destroy (struct hash *h, hash_action_func *destructor) {
	if (destructor != NULL)
		hash_clear (h, destructor);
	free (h->buckets);
}

/* NEW 요소를 해시 테이블 H에 삽입합니다.
   동일한 요소가 테이블에 없을 경우 NULL을 반환하며,
   이미 같은 요소가 존재하면 그 요소를 반환하고 NEW는 삽입하지 않습니다. */
struct hash_elem *
hash_insert (struct hash *h, struct hash_elem *new) {
	struct list *bucket = find_bucket (h, new);
	struct hash_elem *old = find_elem (h, bucket, new);

	if (old == NULL)
		insert_elem (h, bucket, new);

	rehash (h);

	return old;
}

/* NEW를 해시 테이블 H에 삽입하되,
   이미 동일한 요소가 있다면 그 요소를 제거하고 NEW로 교체한 뒤
   교체된 이전 요소를 반환합니다. */
struct hash_elem *
hash_replace (struct hash *h, struct hash_elem *new) {
	struct list *bucket = find_bucket (h, new);
	struct hash_elem *old = find_elem (h, bucket, new);

	if (old != NULL)
		remove_elem (h, old);
	insert_elem (h, bucket, new);

	rehash (h);

	return old;
}

/* 해시 테이블 H에서 E와 같은 요소를 찾아 반환합니다.
   동일한 요소가 없으면 NULL을 반환합니다. */
struct hash_elem *
hash_find (struct hash *h, struct hash_elem *e) {
	return find_elem (h, find_bucket (h, e), e);
}

/* 해시 테이블 H에서 E와 같은 요소를 찾아 제거한 뒤 반환합니다.
   동일한 요소가 없으면 NULL을 반환합니다.

   해시 테이블의 요소가 동적으로 할당되어 있거나 별도의 자원을
   소유하고 있다면, 그 자원을 해제하는 책임은 호출자에게 있습니다. */
struct hash_elem *
hash_delete (struct hash *h, struct hash_elem *e) {
	struct hash_elem *found = find_elem (h, find_bucket (h, e), e);
	if (found != NULL) {
		remove_elem (h, found);
		rehash (h);
	}
	return found;
}

/* 해시 테이블 H의 모든 요소에 대해 ACTION 함수를 임의의 순서로 호출합니다.
   hash_apply()가 실행되는 동안 hash_clear(), hash_destroy(),
   hash_insert(), hash_replace(), hash_delete() 등을 호출하여
   해시 테이블 H를 수정하면 ACTION 내부에서든 외부에서든
   동작이 정의되지 않습니다. */
void
hash_apply (struct hash *h, hash_action_func *action) {
	size_t i;

	ASSERT (action != NULL);

	for (i = 0; i < h->bucket_cnt; i++) {
		struct list *bucket = &h->buckets[i];
		struct list_elem *elem, *next;

		for (elem = list_begin (bucket); elem != list_end (bucket); elem = next) {
			next = list_next (elem);
			action (list_elem_to_hash_elem (elem), h->aux);
		}
	}
}

/* 반복을 위한 구조체 I를 해시 테이블 H에 맞게 초기화합니다.

   사용 예시는 다음과 같습니다:

   struct hash_iterator i;

   hash_first (&i, h);
   while (hash_next (&i))
   {
   		struct foo *f = hash_entry (hash_cur (&i), struct foo, elem);
		... f를 사용하여 무언가를 수행 ...
   }

   반복 중에 hash_clear(), hash_destroy(), hash_insert(),
   hash_replace(), hash_delete() 등을 호출하여 해시 테이블 H를 수정하면
   모든 반복자가 무효화됩니다. */
void
hash_first (struct hash_iterator *i, struct hash *h) {
	ASSERT (i != NULL);
	ASSERT (h != NULL);

	i->hash = h;
	i->bucket = i->hash->buckets;
	i->elem = list_elem_to_hash_elem (list_head (i->bucket));
}

/* 반복자 I를 다음 요소로 이동시키고 그 요소를 반환합니다.
   더 이상 남은 요소가 없다면 NULL을 반환합니다.
   요소는 임의의 순서로 반환됩니다.

   반복 중에 hash_clear(), hash_destroy(), hash_insert(),
   hash_replace(), hash_delete() 등을 호출하여 해시 테이블 H를 수정하면
   모든 반복자가 무효화됩니다. */
struct hash_elem *
hash_next (struct hash_iterator *i) {
	ASSERT (i != NULL);

	i->elem = list_elem_to_hash_elem (list_next (&i->elem->list_elem));
	while (i->elem == list_elem_to_hash_elem (list_end (i->bucket))) {
		if (++i->bucket >= i->hash->buckets + i->hash->bucket_cnt) {
			i->elem = NULL;
			break;
		}
		i->elem = list_elem_to_hash_elem (list_begin (i->bucket));
	}

	return i->elem;
}

/* 현재 반복 중인 요소를 반환합니다.
   더 이상 요소가 없으면 NULL을 반환합니다.
   hash_first() 호출 후 hash_next()를 호출하기 전까지는
   이 함수를 사용하면 안 됩니다. */
struct hash_elem *
hash_cur (struct hash_iterator *i) {
	return i->elem;
}

/* H에 들어 있는 요소의 개수를 반환합니다. */
size_t
hash_size (struct hash *h) {
	return h->elem_cnt;
}

/* H가 비어 있으면 true, 아니면 false를 반환합니다. */
bool
hash_empty (struct hash *h) {
	return h->elem_cnt == 0;
}

/* 32비트 단어 크기를 위한 Fowler-Noll-Vo 해시 상수들. */
#define FNV_64_PRIME 0x00000100000001B3UL
#define FNV_64_BASIS 0xcbf29ce484222325UL

/* BUF가 가리키는 SIZE 바이트 데이터에 대한 해시 값을 반환합니다. */
uint64_t
hash_bytes (const void *buf_, size_t size) {
	/* 바이트 데이터를 위한 Fowler-Noll-Vo 32비트 해시 방식. */
	const unsigned char *buf = buf_;
	uint64_t hash;

	ASSERT (buf != NULL);

	hash = FNV_64_BASIS;
	while (size-- > 0)
		hash = (hash * FNV_64_PRIME) ^ *buf++;

	return hash;
}

/* 문자열 S에 대한 해시 값을 반환합니다. */
uint64_t
hash_string (const char *s_) {
	const unsigned char *s = (const unsigned char *) s_;
	uint64_t hash;

	ASSERT (s != NULL);

	hash = FNV_64_BASIS;
	while (*s != '\0')
		hash = (hash * FNV_64_PRIME) ^ *s++;

	return hash;
}

/* 정수 I에 대한 해시 값을 반환합니다. */
uint64_t
hash_int (int i) {
	return hash_bytes (&i, sizeof i);
}

/* 해시 테이블 H에서 요소 E가 속한 버킷을 반환합니다. */
static struct list *
find_bucket (struct hash *h, struct hash_elem *e) {
	size_t bucket_idx = h->hash (e, h->aux) & (h->bucket_cnt - 1);
	return &h->buckets[bucket_idx];
}

/* BUCKET에서 E와 동일한 해시 요소를 찾아 반환합니다.
   찾지 못하면 NULL을 반환합니다. */
static struct hash_elem *
find_elem (struct hash *h, struct list *bucket, struct hash_elem *e) {
	struct list_elem *i;

	for (i = list_begin (bucket); i != list_end (bucket); i = list_next (i)) {
		struct hash_elem *hi = list_elem_to_hash_elem (i);
		if (!h->less (hi, e, h->aux) && !h->less (e, hi, h->aux))
			return hi;
	}
	return NULL;
}

/* X의 가장 낮은 위치에 있는 1비트를 없앤 값을 반환합니다. */
static inline size_t
turn_off_least_1bit (size_t x) {
	return x & (x - 1);
}

/* X가 2의 거듭제곱이면 true, 그렇지 않으면 false를 반환합니다. */
static inline size_t
is_power_of_2 (size_t x) {
	return x != 0 && turn_off_least_1bit (x) == 0;
}

/* 버킷 하나당 포함되길 기대하는 요소의 수. */
#define MIN_ELEMS_PER_BUCKET  1 /* 버킷당 요소 수가 1 미만이면 버킷 수를 줄입니다. */
#define BEST_ELEMS_PER_BUCKET 2 /* 이상적인 버킷당 요소 수. */
#define MAX_ELEMS_PER_BUCKET  4 /* 버킷당 요소 수가 4를 넘으면 버킷 수를 늘립니다. */

/* 이상적인 비율에 맞추기 위해 해시 테이블 H의 버킷 수를 조정합니다.
   메모리가 부족하면 실패할 수도 있지만, 그 경우 단지 접근 효율이
   떨어질 뿐 계속 사용은 가능합니다. */
static void
rehash (struct hash *h) {
	size_t old_bucket_cnt, new_bucket_cnt;
	struct list *new_buckets, *old_buckets;
	size_t i;

	ASSERT (h != NULL);

	/* 이전 버킷 정보를 보관해 둡니다. */
	old_buckets = h->buckets;
	old_bucket_cnt = h->bucket_cnt;

	/* 새로 사용할 버킷 수를 계산합니다.
		BEST_ELEMS_PER_BUCKET 개의 요소마다 버킷 하나가 되도록 하며
		최소 4개의 버킷을 유지해야 하고, 버킷 수는 2의 거듭제곱이어야 합니다. */
	new_bucket_cnt = h->elem_cnt / BEST_ELEMS_PER_BUCKET;
	if (new_bucket_cnt < 4)
		new_bucket_cnt = 4;
	while (!is_power_of_2 (new_bucket_cnt))
		new_bucket_cnt = turn_off_least_1bit (new_bucket_cnt);

	/* 버킷 수가 변하지 않는다면 아무 작업도 하지 않습니다. */
	if (new_bucket_cnt == old_bucket_cnt)
		return;

	/* 새 버킷을 할당하고 비어 있는 상태로 초기화합니다. */
	new_buckets = malloc (sizeof *new_buckets * new_bucket_cnt);
	if (new_buckets == NULL) {
		/* 메모리 할당에 실패한 경우입니다.
 		   해시 테이블 사용 효율이 떨어질 뿐 여전히 사용 가능하므로
		   오류로 취급하지 않습니다. */
		return;
	}
	for (i = 0; i < new_bucket_cnt; i++)
		list_init (&new_buckets[i]);

	/* 새 버킷 정보를 저장합니다. */
	h->buckets = new_buckets;
	h->bucket_cnt = new_bucket_cnt;

	/* 기존 모든 요소를 알맞은 새 버킷으로 옮깁니다. */
	for (i = 0; i < old_bucket_cnt; i++) {
		struct list *old_bucket;
		struct list_elem *elem, *next;

		old_bucket = &old_buckets[i];
		for (elem = list_begin (old_bucket);
				elem != list_end (old_bucket); elem = next) {
			struct list *new_bucket
				= find_bucket (h, list_elem_to_hash_elem (elem));
			next = list_next (elem);
			list_remove (elem);
			list_push_front (new_bucket, elem);
		}
	}

	free (old_buckets);
}

/* 해시 테이블 H의 BUCKET에 요소 E를 삽입합니다. */
static void
insert_elem (struct hash *h, struct list *bucket, struct hash_elem *e) {
	h->elem_cnt++;
	list_push_front (bucket, &e->list_elem);
}

/* 해시 테이블 H에서 요소 E를 제거합니다. */
static void
remove_elem (struct hash *h, struct hash_elem *e) {
	h->elem_cnt--;
	list_remove (&e->list_elem);
}

