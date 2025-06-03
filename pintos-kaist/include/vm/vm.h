#ifndef VM_VM_H
#define VM_VM_H
#include <stdbool.h>
#include "threads/palloc.h"

enum vm_type {
	/*아직 초기화되지 않은 페이지*/
	VM_UNINIT = 0,
	/* 파일과 무관한, 익명 페이지 */
	VM_ANON = 1,
	/* 파일에 연결된 페이지 */
	VM_FILE = 2,
	/* 파일 시스템용 페이지 캐시(project 4용) */
	VM_PAGE_CACHE = 3,

	/* Bit flags to store state */

	/* Auxillary bit flag marker for store information. You can add more
	 * markers, until the value is fit in the int. */
	VM_MARKER_0 = (1 << 3),
	VM_MARKER_1 = (1 << 4),

	/* DO NOT EXCEED THIS VALUE. */
	VM_MARKER_END = (1 << 31),
};

#include "vm/uninit.h"
#include "vm/anon.h"
#include "vm/file.h"
#ifdef EFILESYS
#include "filesys/page_cache.h"
#endif

struct page_operations;
struct thread;

#define VM_TYPE(type) ((type) & 7)

/*
=============================================================================== 
  The representation of "page".
 * This is kind of "parent class", which has four "child class"es, which are
 * uninit_page, file_page, anon_page, and page cache (project4).
 * DO NOT REMOVE/MODIFY PREDEFINED MEMBER OF THIS STRUCTURE. 
 * 
 
 [struct page]
 
 * 가상 메모리에서의 하나의 페이지를 나타내는 핵심 구조체
 * OOP 스타일의 "부모 클래스" 같은 역할
 * 사용자 코드 기준의 주소 단위(가상 공간)
 * 모든 페이지의 공통 정보를 담고 있다.
   => 어떤 페이지인지에 따라 union 안에서 struct 가 선택적으로 포함된다.
 ==============================================================================
 * */
struct page {
	const struct page_operations *operations; //해당 페이지가 사용할 연산들(함수 포인터 테이블)
	void *va;              /*사용자 공간상의 주소(virtual address)*/
	struct frame *frame;   /* 실제 물리 메모리와 연결된 frame */

	/* Your implementation */

	//이 페이지가 어떤 종류인지에 따라 구조체 선택(uninit/anon/file등) => type
	union {
		struct uninit_page uninit; //초기화 안된 페이지
		struct anon_page anon; //익명 페이지
		struct file_page file; //파일 페이지
#ifdef EFILESYS
		struct page_cache page_cache; //페이지 캐시(프로젝트 4)
#endif
	};
};

/*
============================================ 
The representation of "frame" 

-이 구조체는 물리 메모리의 프레임을 나타낸다.
-실제 메모리의 할당 단위

-gitbook (필요한 멤버를 추가해도 된다.)
============================================
*/
struct frame {
	void *kva; //커널(kernel) 가상(virtual) 주소(address)
	struct page *page; //이 프레임과 매핑된 가상 페이지
};



/*
=====================================================================
 The function table for page operations.
 * This is one way of implementing "interface" in C.
 * Put the table of "method" into the struct's member, and
 * call it whenever you needed.
 * 
 
 [page_operations]

 * 각 page가 사용할 수 있는 동작을 정의한 함수 포인터 테이블
 * page의 type에 맞는 동작을 구현한다.
   
   1.swap_in :디스크에서 메모리로 페이지를 로드하는 함수 포인터
   2.swap_out : 메모리에서 디스크로 페이지를 스왑 아웃하는 함수 포인터
   3.destroy : 페이지를 제거하는 함수 포인터
   4.type : 페이지의 타입을 나타내는 열거형.

   //
 ======================================================================
 *  */
struct page_operations {
	bool (*swap_in) (struct page *, void *);
	bool (*swap_out) (struct page *);
	void (*destroy) (struct page *);
	enum vm_type type;
};

#define swap_in(page, v) (page)->operations->swap_in ((page), v)
#define swap_out(page) (page)->operations->swap_out (page)
#define destroy(page) \
	if ((page)->operations->destroy) (page)->operations->destroy (page)

/* 
===========================================================================
   Representation of current process's memory space.
 * We don't want to force you to obey any specific design for this struct.
 * All designs up to you for this.  
 
 [보조 페이지 테이블]

* 기본 테이블이 표현하지 못하는 정보를 추가로 담기 위해 필요
* 페이지 폴트 처리 및 리소스 관리를 위해 필요하다.

===========================================================================
*/
struct supplemental_page_table {
};

#include "threads/thread.h"

/*spt를 위한 함수들*/
void supplemental_page_table_init (struct supplemental_page_table *spt);
bool supplemental_page_table_copy (struct supplemental_page_table *dst,
		struct supplemental_page_table *src);
void supplemental_page_table_kill (struct supplemental_page_table *spt);
struct page *spt_find_page (struct supplemental_page_table *spt,
		void *va);
bool spt_insert_page (struct supplemental_page_table *spt, struct page *page);
void spt_remove_page (struct supplemental_page_table *spt, struct page *page);



void vm_init (void);
bool vm_try_handle_fault (struct intr_frame *f, void *addr, bool user,
		bool write, bool not_present);

#define vm_alloc_page(type, upage, writable) \
	vm_alloc_page_with_initializer ((type), (upage), (writable), NULL, NULL)
bool vm_alloc_page_with_initializer (enum vm_type type, void *upage,
		bool writable, vm_initializer *init, void *aux);
void vm_dealloc_page (struct page *page);
bool vm_claim_page (void *va);
enum vm_type page_get_type (struct page *page);

#endif  /* VM_VM_H */
