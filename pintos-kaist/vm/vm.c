/*
=========================================================== 
vm.c: 가상 메모리 객체를 위한 일반적인 인터페이스입니다.
-인터페이스 : 어떤 객체가 가져가야 할 행동의 목록 정의해놓은 파일
===========================================================
 */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

/* 각 서브시스템의 초기화 코드를 호출하여
 * 가상 메모리 하위 시스템을 초기화합니다. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
        /* 위의 줄은 수정하지 마세요. */
        /* TODO: 여기에 코드를 작성하세요. */
}

/* 페이지의 유형을 얻습니다. 초기화된 이후에 어떤 타입이 될지
 * 알고 싶을 때 유용하며, 이 함수는 이미 완전히 구현되어 있습니다. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* 헬퍼 함수들 */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* 초기화 함수를 가지고 미리 생성해 두는 페이지 객체를 만듭니다.
 * 페이지를 직접 만들지 말고 이 함수나 `vm_alloc_page`를 통해 생성하세요. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

        /* upage가 이미 사용 중인지 확인합니다. */
	if (spt_find_page (spt, upage) == NULL) {
    /* TODO: 페이지를 만들고 VM 타입에 맞는 initializer를 얻은 뒤
    * TODO: uninit_new를 호출하여 "uninit" 페이지 구조체를 생성합니다.
    * TODO: 호출 후 필요한 필드를 수정해야 합니다. */
    /* TODO: 생성한 페이지를 spt에 넣어주세요. */
    
	bool (*page_initializer)(struct page *,enum vm_type,void *kva);
	struct page *page =malloc(sizeof(struct page));


	switch(VM_TYPE(type))
	{
	case VM_ANON:
	   page_initializer =anon_initializer;
	   break;	
	case VM_FILE:
	   page_initializer=file_backed_initializer;
	   break;
	default:
	   free(page);
	   goto err;
	   break;
	}
	uninit_new(page,upage,init,type,aux,page_initializer);
	page->writable=writable;
    

	return spt_insert_page(spt,page);

	}
err:
	return false;
}

/* 
===============================================
[spt_find_page]

spt에서 VA에 해당하는 페이지를 찾아 반환합니다. 
실패 시 NULL을 돌려줍니다. 
===============================================
*/
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = NULL;


        /* TODO: 이 함수를 완성하세요. */

	return page;
}

/* 
=========================================
[spt_insert_page]

PAGE를 spt에 검증 후 삽입합니다. 

이 함수는 해당 가상 주소가 
이미 보조 테이블에 존재하지 않는지 
확인해야 한다.
=========================================
*/
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
        /* TODO: 이 함수를 완성하세요. */

	return succ;
}

/*
=======================================
[spt_remove_page]

보조테이블에서 페이지를 삭제하는 함수
메모리 할당을 해제 해준다.
=======================================
*/
void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/*
=============================================================================
                              프레임 관련 함수들
=============================================================================
*/



/* 앞으로 쫓아낼 프레임을 얻습니다. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
         /* TODO: 어떤 프레임을 제거할지는 구현하기 나름입니다. */

	return victim;
}

/* 한 페이지를 교체하고 그에 해당하는 프레임을 반환합니다.
 * 실패하면 NULL을 반환합니다.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
        /* TODO: victim을 swap 영역으로 내보내고, 그 프레임을 반환하세요. */

	return NULL;
}

/* 
======================================================================
[vm_get_frame]
 * 
 * palloc()을 이용해 프레임을 얻습니다.(프레임 할당)
 * 남는 페이지가 없다면 하나를 제거하여 돌려줍니다.
  
 * 즉 사용자 풀 메모리가 가득 차도 이 함수는 프레임을 얻기 위해 
 * 기존 프레임을 내보낸 뒤 유효한 주소를 반환합니다.
 
 * 실패 시 스왑아웃 처리는 여기선 필요 없음, PANIC("todo")로 처리해도 됨
======================================================================
*/

static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
        /* TODO: 이 함수를 완성하세요. */

	//1.물리 메모리 1페이지 할당 (kva,palloc_get_page(PAL_USER))

	//2. frame 구조체 동적 할당

	//3.frame 구조체 안의 kva 멤버를 다시 지정
	
	/*
	4.frame 안의 page NULL로 지정 
	  
	  이 프레임은 아직 아무 페이지와도 연결되지 않음

	  나중에 연결 여부 판단 => vm_do_claim_page 등에서 검사
    */
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}




/* 스택을 확장합니다. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* write_protected 페이지에서의 fault 처리 */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/*
============================================== 
성공하면 true를 반환합니다 

페이지 폴트 예외가 발생했을 때 OS가 해당 문제를
해결하는 핵심 함수 아래의 흐름으로 처리한다.


1. 폴트 난 가상 주소를 보조 페이지 테이블에서 검색
    
→ 유효하면, 파일/스왑/제로 등에서 데이터를 가져올 준비
    
→ 유효하지 않으면(커널 영역/쓰기 불가 영역/권한 없는 접근 등), 프로세스 종료
    
2. 실제 데이터를 저장할 프레임 확보(프레임 테이블/페이지 교체)
3. 해당 데이터를 프레임에 읽어오기(파일/스왑/제로 등)
4. 페이지 테이블 엔트리를 해당 프레임에 연결(threads/mmu.c 함수 활용)
==============================================
*/
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {


	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;

	struct page *page = spt_find_page(spt,addr);
	
	//1. spt_find_page
	//=> uninit_initialize() 호출

	//2.권한 검사 
        /* TODO: fault가 유효한지 확인하세요  
		=> 유효한 페이지 폴트라면 , 초기화 함수 실행 (lazy_load_segment())
		
		*/
        /* TODO: 필요한 로직을 작성합니다
		 */
    if(page == NULL){
		return false;
	}
	return vm_do_claim_page (page);
	//프레임 할당 + 내용로딩 + 페이지 테이블 연결
}

/* 페이지를 해제합니다.
 * 이 함수는 수정하지 마세요. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* 
======================================
VA에 할당된 페이지를 확보합니다. 

-주소 va에 대해 struct page를 찾아오고
-vm_do_claim_page를 호출한다.
======================================
*/
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
        /* TODO: 이 함수를 완성하세요 */

	return vm_do_claim_page (page);
}

/* 
================================================================
[vm_do_claim_page]

-주어진 페이지를 물리 프레임에 연결한다.

-내부적으로 vm_get_frame()을 호출해 프레임을 할당
 PAGE를 확보하고 MMU 설정을 완료합니다. 

-페이지 테이블에 가상주소 -> 물리주소 매핑을 추가
-성공 여부를 반환해야 한다.
================================================================
*/
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame (); 
	//이 함수 안에서 return 값으로 넘겨 받기

	/* Set links */
	frame->page = page;
	page->frame = frame;

    /* TODO: "페이지의 VA"와 "프레임의 PA"를 매핑하는 항목을 삽입하세요. */

	if(!pml4_set_page(thread_current()->pml4, page->va, frame->kva,page->writable)){
		PANIC("TODO");
	}

	return swap_in (page, frame->kva);
}





/* 
====================================================
[void supplemental_page_table_init]

-새로운 supplemental page table을 초기화합니다.

-어떤 자료구조를 사용할지는 자유이다.

-이 함수는 새 프로세스가 시작될 때 (process.c의 initd)

-그리고 프로세스가 fork될 때 (process.c의 _do_fork) 호출

====================================================
   */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
}



/* 
========================================================
  src에서 dst로 supplemental page table을 복사한다.

  =>우현 오빠 설명 
========================================================
*/
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {



}

/* supplemental page table이 가진 자원을 해제합니다 */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
        /* TODO: 스레드가 보유한 모든 supplemental_page_table을 파괴하고
         * TODO: 수정된 내용을 저장소에 모두 반영하세요. */
}
