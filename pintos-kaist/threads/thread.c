
//=== [1] Include Headers ===//
#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/timer.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif


//=== [2] Thread Constants ===//
#define THREAD_MAGIC 0xcd6abf4b   // Used to detect stack overflow
#define THREAD_BASIC 0xd42df210   // Do not modify
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))	
		// 현재 CPU의 rsp 값을 페이지 경계까지 내림하여 해당 스레드 포인터 반환
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)
		// 유효한 thread 구조체인지 확인하는 매크로 (스택 오버플로 감지 목적)
#define TIME_SLICE 4              // # of timer ticks to give each thread


//=== [3] Thread Lists & Global State ===//
static struct list ready_list;         // 스레드 READY 상태 큐
static struct list sleep_list;         // BLOCKED 상태 큐 (알람 용도)
static struct list wait_list;		   // ❓
static struct list destruction_req;    // 제거 대기 중인 스레드 리스트

static struct thread *idle_thread;     // idle 상태의 스레드 포인터
static struct thread *initial_thread;  // main()을 실행하는 최초 스레드 포인터

static int64_t awake_closest_tick;     // 다음으로 깨워야 할 tick = 가장 빠른 wakeup tick 저장
static unsigned thread_ticks;          // 최근 타임슬라이스 틱 수 = 마지막 yield 이후의 ticks

/* 통계용 틱 카운터 */
static long long idle_ticks;		   // ❓
static long long kernel_ticks;		   // ❓
static long long user_ticks;		   // ❓

/* tid 할당용 락 */
static struct lock tid_lock;		   // TID 할당용 락

/* 스케줄러 설정 */
bool thread_mlfqs;					   // MLFQ 스케줄러 사용 여부


//=== [4] GDT 초기화용 커널 전용 GDT ===//
static uint64_t gdt[3] = {
    0,
    0x00af9a000000ffff, // Kernel code segment
    0x00cf92000000ffff  // Kernel data segment
};


//=== [5] Static Function Declarations ===//

/* ------------------ Scheduler ------------------ */
static void do_schedule (int status);
static void schedule (void);
static struct thread *next_thread_to_run (void);

/* ------------------ Thread Lifecycle ------------------ */
static void kernel_thread (thread_func *, void *aux);
static void idle (void *idle_started_);
static void init_thread (struct thread *t, const char *name, int priority);
static tid_t allocate_tid (void);

/* ------------------ Ready/Sleep Queue Compare Functions ------------------ */
bool cmp_priority (const struct list_elem *a, const struct list_elem *b, void *aux);
static bool cmp_wakeup_tick (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
static void preempt_priority(void);

/* ------------------ Debug Utilities ------------------ */
// static void debug_print_thread_lists (void);    // 디버깅용 리스트 출력 함수


/**********************************************************
 * thread_init - Pintos 스레드 시스템 초기화
 *
 * 기능:
 * - 현재 실행 중인 코드를 하나의 스레드로 변환
 * - GDT(Global Descriptor Table)를 임시 값으로 설정
 * - ready_list, sleep_list, destruction_req 리스트 초기화
 * - tid_lock 초기화 및 최초 실행 스레드 설정
 *
 * 주의:
 * - loader.S가 스택을 페이지 경계에 정렬했기 때문에 가능함
 * - thread_current()는 이 함수가 끝나기 전까지 사용 불가
 *
 * 호출 순서:
 * - page allocator 초기화 전에 반드시 호출되어야 함
 **********************************************************/
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);  
	// 인터럽트 비활성 상태인지 확인 (초기화 중에는 인터럽트가 꺼져 있어야 함)

	/* Reload the temporal gdt for the kernel */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,            // GDT 크기 설정
		.address = (uint64_t) gdt            // GDT 주소 설정
	};
	lgdt (&gdt_ds);                           // GDT 레지스터에 설정값 로드

	/* Init the global thread context */
	lock_init (&tid_lock);                   // TID 할당을 위한 락 초기화
	list_init (&ready_list);                 // 준비 상태 스레드 리스트 초기화
	list_init (&sleep_list);                 // ⏰ sleep 상태 스레드 리스트 초기화
	list_init (&wait_list);					 // ❓
	list_init (&destruction_req);            // 제거 요청 대기 스레드 리스트 초기화

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();      // 현재 실행 중인 스레드를 thread 구조체로 변환
	init_thread (initial_thread, "main", PRI_DEFAULT);  // 초기 스레드 이름과 우선순위 설정
	initial_thread->status = THREAD_RUNNING; // 현재 실행 중 상태로 표시
	initial_thread->tid = allocate_tid ();   // TID 할당
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling. */
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/*************************************************************
 * thread_create - 새로운 커널 스레드를 생성하고 ready_list에 추가
 *
 * 기능:
 * - 이름(name), 우선순위(priority), 실행할 함수(function), 인자(aux)를 받아
 *   새로운 커널 스레드를 초기화하고 스케줄링 가능한 상태로 만듬
 *
 * 반환값:
 * - 생성된 스레드의 tid (성공 시)
 * - TID_ERROR (메모리 할당 실패 등 오류 시)
 *
 * 주의사항:
 * - thread_start()가 호출된 이후라면, 이 함수가 리턴되기 전에
 *   새 스레드가 실행되거나 종료될 수도 있음 (동기화 필요)
 * - 우선순위 기반 스케줄링은 구현되어 있지 않지만, 구조는 지원함
 *************************************************************/
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);			// 실행할 함수는 NULL일 수 없음

	/* 1. 스레드 구조체 메모리 할당 및 0으로 초기화 */
	t = palloc_get_page (PAL_ZERO);   	// PAL_ZERO: 할당 후 0으로 초기화
	if (t == NULL)
		return TID_ERROR;				// 메모리 할당 실패 시 오류 반환

	/* 2. 스레드 초기화 및 TID 설정 */
	init_thread (t, name, priority);     // 이름과 우선순위 설정
	tid = t->tid = allocate_tid ();      // 고유한 TID 할당

	/* 3. 새 스레드가 실행할 함수와 컨텍스트 설정 */
	t->tf.rip = (uintptr_t) kernel_thread;	// 실행 시작 지점을 kernel_thread로 설정
	t->tf.R.rdi = (uint64_t) function;      // 첫 번째 인자로 실행할 함수 전달
	t->tf.R.rsi = (uint64_t) aux;           // 두 번째 인자로 함수 인자 전달
	t->tf.ds = SEL_KDSEG;                   // 디데이터 세그먼트 설정
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;                   // 스택 세그먼트
	t->tf.cs = SEL_KCSEG;                   // 코드 세그먼트
	t->tf.eflags = FLAG_IF;                 // 인터럽트 플래그 설정

	/* 4. 스레드를 READY 상태로 전환하고 ready_list에 삽입 */
	thread_unblock (t);
	
	/** project1-Priority Scheduling */
	if(t->priority > thread_current()->priority)
		thread_yield();

	// preempt_priority();	// 🔥 removed: thread_unblock already handles preemption logic

	return tid;								// 생성된 스레드의 ID 반환
}

/**********************************************************
 * thread_sleep - 현재 실행 중인 스레드를 지정한 틱 수만큼 재움
 *
 * 기능:
 * - idle_thread는 재우지 않음
 * - 현재 스레드를 sleep_list에 추가하고 BLOCKED 상태로 전환
 * - wakeup_ticks을 현재 시간 + ticks로 설정
 * - (global) awake_closest_tick 갱신
 * - thread_block() 호출로 스케줄러 대상에서 제외
 *
 * 동기화:
 * - 인터럽트를 비활성화한 상태에서 sleep_list에 접근
 *
 * 호출:
 * - timer_sleep() 함수에서 호출됨
 **********************************************************/
void
thread_sleep (int64_t wakeup_tick) 
{
    struct thread *cur = thread_current(); // 현재 실행중인 스리드 해석

    if (cur == idle_thread) return;       // idle 스리드는 잠을 할 필요 없음

    enum intr_level old_level = intr_disable(); // 동기화 복잡화 목적으로 인터럽트 비활성화

    cur->wakeup_ticks = wakeup_tick; // 지정된 잠을 기간의 종료 tick을 저장
    update_closest_tick(wakeup_tick); // 가장 보기 가까운 tick 갱신
    list_insert_ordered(&sleep_list, &cur->elem, cmp_wakeup_tick, NULL); // wakeup_tick 까지 정렬된 순서로 삽입
    thread_block(); // 현재 스리드를 BLOCKED 상태로 변경 후 스케줄러 대상\uc5d에서 제제

    intr_set_level(old_level); // 이전 인터럽트 상태로 복원

    /* ❌ 기존 busy-wait 구조에서 지정 (AS-IS)
    cur->wakeup_ticks = timer_ticks() + ticks;      // 현재 시간 + ticks 값 계산해서 wakeup_ticks 설정 */

    // // 중복 삽입 방지를 위해 무적가로써 이미 sleep_list에 포함되어 있는지 확인
    // if (list_contains(&sleep_list, &cur->elem))
    // 	list_remove(&cur->elem); // 포함되었다면 제거
}


static bool 
cmp_wakeup_tick(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
    struct thread *ta = list_entry(a, struct thread, elem);
    struct thread *tb = list_entry(b, struct thread, elem);
    return ta->wakeup_ticks < tb->wakeup_ticks;
}


/*************************************************************
 * thread_awake - 슬립 리스트에서 깨울 시간이 지난 스레드들을 깨움
 *
 * 기능:
 * - sleep_list에 있는 스레드 중, current_tick ≤ 현재 tick인 스레드를 READY 상태로 전환
 * - list_remove() 및 thread_unblock()을 통해 스레드 깨움
 * - 남아 있는 스레드들의 current_tick 중 가장 이른 값으로 awake_closest_tick 갱신
 *
 * 동기화:
 * - 인터럽트 컨텍스트에서 실행되므로 별도 락 불필요
 * - list_remove() 시 반복자 갱신에 주의 필요
 *
 * 호출 위치:
 * - timer_interrupt() 내부에서, ticks ≥ awake_closest_tick일 때 호출
 *
 * 제약 조건:
 * - BLOCKED 상태가 아닌 스레드를 깨우면 안 됨 (thread_unblock 제약)
 * - thread_block() 호출 금지 (인터럽트 컨텍스트이므로)
 *
 * 요구사항:
 * - busy-wait 없이 정확한 tick 기반 sleep/wakeup 동작 보장
 * - awake_closest_tick 값을 매 tick마다 갱신하여 불필요한 검사 최소화
 *************************************************************/
void
thread_awake (int64_t current_tick)
{
    awake_closest_tick = INT64_MAX;  // 슬립 리스트를 순회하며 가장 빠른 wakeup_tick으로 초기화

    struct list_elem *sleeping = list_begin(&sleep_list);  // sleep_list 순회 시작

    while (sleeping != list_end(&sleep_list)) {
        struct thread *th = list_entry(sleeping, struct thread, elem);  // 요소를 thread 구조체로 변환

        if (current_tick >= th->wakeup_ticks && th->status == THREAD_BLOCKED) {
            struct list_elem *next = list_remove(sleeping);  // 리스트에서 제거 후 다음 요소 저장
            thread_unblock(th);  // BLOCKED 상태 → READY 상태로 전환
            sleeping = next;  // 다음 요소로 이동
        } else {
            update_closest_tick(th->wakeup_ticks);  // 남은 thread 중 가장 가까운 wakeup_tick 갱신
            break;  // 오름차순 정렬이므로 더 이상 확인할 필요 없음
        }
    }
}

/*************************************************************
 * update_closest_tick - 슬립 리스트 내 최소 wakeup_tick을 갱신
 *
 * 기능:
 * - 현재 tick 값이 awake_closest_tick보다 작으면 갱신
 * - 다음 thread_awake() 시점 결정을 위해 사용됨
 *************************************************************/
void
update_closest_tick (int64_t ticks) 
{
	// 기존 awake_closest_tick보다 더 빠른 tick이라면 갱신
	awake_closest_tick = (awake_closest_tick > ticks) ? ticks : awake_closest_tick;
}

/*************************************************************
 * closest_tick - 현재 저장된 가장 이른 wakeup tick 반환
 *
 * 기능:
 * - thread_awake() 호출 여부 판단을 위해 사용됨
 *************************************************************/
int64_t
closest_tick (void)
{
	return awake_closest_tick;
}

/*************************************************************
 * thread_block - 현재 실행 중인 스레드를 BLOCKED 상태로 전환
 *
 * 기능:
 * - 현재 스레드의 상태를 THREAD_BLOCKED로 설정하고,
 *   스케줄러를 호출하여 다른 스레드로 전환함
 * - 이 함수에 의해 차단된 스레드는 이후 thread_unblock()에 의해 깨워질 때까지 실행되지 않음
 *
 * 제약 조건:
 * - 반드시 인터럽트가 비활성화된 상태에서 호출되어야 함
 *   (스레드 상태 전환 중 동기화 문제 방지 목적)
 *
 * 참고:
 * - 일반적인 조건 변수, 세마포어 등을 통한 동기화에는 synch.h의 고수준 API 사용 권장
 *************************************************************/
void
thread_block (void) 
{
	ASSERT (!intr_context ());             		// 인터럽트 핸들러 내에서 호출되면 안 됨
	ASSERT (intr_get_level () == INTR_OFF); 	// 인터럽트가 꺼진 상태여야 안전함

	thread_current ()->status = THREAD_BLOCKED; // 현재 스레드 상태를 BLOCKED로 설정
	schedule();                                 // 스케줄러 호출하여 문맥 전환 수행
}

/*************************************************************
 * thread_unblock - BLOCKED 상태의 스레드를 READY 상태로 전환
 *
 * 기능:
 * - BLOCKED 상태의 스레드를 ready_list에 우선순위 순으로 삽입
 * - 스레드의 상태를 THREAD_READY로 변경
 * - 현재 running 중인 스레드를 선점하지는 않음 (스케줄링은 호출자 책임)
 *
 * 구현:
 * - 인터럽트를 비활성화하여 atomic하게 ready_list 수정
 * - cmp_priority 함수 기준으로 우선순위 삽입 (높은 우선순위 먼저)
 * - 함수 종료 시 인터럽트 상태 복원
 *
 * 주의:
 * - 반드시 THREAD_BLOCKED 상태의 스레드만 인자로 받아야 함
 * - caller가 interrupt를 disable한 상황에서도 동작 가능해야 하므로,
 *   이 함수는 스레드를 깨우되, 직접 스케줄링은 하지 않음
 *************************************************************/
void
thread_unblock (struct thread *t) 
{
	enum intr_level old_level;				// 인터럽트 상태 저장용 (함수 끝에서 복원할 값)

	ASSERT (is_thread (t));					// 전달된 포인터가 유효한 스레드 구조체인지 확인

	old_level = intr_disable ();			// 인터럽트 비활성화 (원자적 작업 보장)
	ASSERT (t->status == THREAD_BLOCKED);	// BLOCKED 상태인지 검증 (그 외 상태면 잘못된 호출)
	// printf("[UNBLOCK] %s inserted into ready_list (priority: %d)\n", t->name, t->priority);
	// debug_print_thread_lists();

	list_insert_ordered (&ready_list, &t->elem, cmp_priority, NULL);
						// ready_list에 우선순위 기준 정렬 삽입 (높은 우선순위가 앞쪽)
	t->status = THREAD_READY;				// 스레드 상태를 READY로 전환

	intr_set_level (old_level);				// 인터럽트 상태 복원 → 인터럽트가 켜진 상태에서만 안전하게 선점 우위 판단 가능
	// preempt_priority();						// 선점 우위 판단 → thread_yield() 가능
}

/* =============================================================
 * preempt_priority - 현재 스레드보다 높은 우선순위 스레드가 ready_list에 있으면 선점
 *
 * 호출 위치:
 * - thread_unblock(), priority update 등 스레드가 READY 상태로 전환되는 순간
 *
 * 기능:
 * - 인터럽트 컨텍스트가 아닐 경우, ready_list의 맨 앞 스레드와 현재 스레드의 우선순위를 비교
 * - 우선순위가 더 높은 스레드가 있다면 현재 스레드는 thread_yield()를 호출해 CPU 양보
 *
 * 주의:
 * - intr_context() 내부에선 yield를 하면 안 되므로 반드시 조건 체크
 * ============================================================= */
void
preempt_priority(void) 
{
    if (!intr_context() && !list_empty(&ready_list)) {	// 인터럽트 핸들러 안에서 실행 중이 아니고, 리스트가 비어있지 않은 경우에만 선점 검사 수행
        struct thread *cur = thread_current();			// 현재 실행 중인 스레드의 포인터를 가져옴
        struct thread *front = list_entry(list_front(&ready_list), struct thread, elem);	// 리스트의 맨 앞(우선순위 높은) 스레드를 가져옴

		// printf("[PREEMPT] Current: %s (%d), Front: %s (%d)\n", cur->name, cur->priority, front->name, front->priority);
        if (cur->priority < front->priority) {			// 만약 현재 스레드보다 더 높은 우선순위의 스레드가 리스트에 있다면
            // printf("[PREEMPT] Current: %s (%d), Front: %s (%d)\n", cur->name, cur->priority, front->name, front->priority);~
			thread_yield();								// 현재 스레드는 자발적으로 CPU를 양보하여 스케줄러가 다른 스레드를 실행하게 함
        }
    }
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/*************************************************************
 * thread_current - 현재 CPU에서 실행 중인 스레드를 반환
 *
 * 기능:
 * - running_thread()를 통해 현재 스택 포인터 기반으로 스레드 구조체를 얻음
 * - 해당 구조체가 유효한 스레드인지 두 가지 ASSERT로 검증
 * - 최종적으로 현재 실행 중인 thread 포인터를 반환
 *
 * 주의:
 * - Pintos는 각 스레드를 독립적인 커널 스택과 페이지에 배치하므로,
 *   스택 포인터를 페이지 기준으로 내림(pg_round_down)하여
 *   현재 스레드를 역추적할 수 있음
 *
 * 검증:
 * - is_thread(t): thread magic number 확인
 * - t->status == THREAD_RUNNING: 실행 중 상태인지 확인
 *************************************************************/
struct thread *
thread_current (void) 
{
	struct thread *t = running_thread (); // 현재 스택 포인터 기반으로 thread 구조체 추론

	/* t가 유효한 스레드인지 확인 (magic 필드 검사) */
	/* 현재 스레드 상태가 실행 중인지 확인 */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t; // 현재 실행 중인 스레드 포인터 반환
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/*************************************************************
 * thread_yield - 현재 스레드가 CPU 양보 (자발적 컨텍스트 스위칭 요청)
 *
 * 기능:
 * - 현재 스레드를 sleep 상태로 만들지 않고 ready_list로 이동
 * - 이후 스케줄러를 통해 다른 스레드가 실행될 수 있도록 함
 * - idle_thread는 다시 ready_list에 넣지 않음 (특수 스레드이기 때문)
 *
 * 구현:
 * - 인터럽트를 비활성화한 후 ready_list에 현재 스레드를 삽입
 * - 상태를 THREAD_READY로 바꾸고 do_schedule 호출!~
 * - 이후 인터럽트 상태를 복원
 *
 * 주의:
 * - 인터럽트 핸들러 내부에서 호출해서는 안 됨 (ASSERT로 검증)
 *************************************************************/
void
thread_yield (void) {
	struct thread *cur = thread_current ();		// 현재 실행중인 스레드 구조체 반환
	enum intr_level old_level;					// 인터럽트 상태 저장용 변수

	ASSERT (!intr_context ());		// 인터럽트 핸들러 내에서는 호출 불가 (중첩 스케쥴링 방지)
	old_level = intr_disable ();	// 인터럽트를 비활성화(ready_list 수정 중 동기화 필요)하고 이전 상태를 리턴
	
	if (cur != idle_thread)			// 현재 스레드가 idle이 아니라면 ready_list에 우선순위 기준으로 삽입
		list_insert_ordered (&ready_list, &cur->elem, cmp_priority, NULL);
	
	do_schedule (THREAD_READY);		// 현재 스레드 상태를 THREAD_READY로 바꾸고 스케줄링 수행
	intr_set_level (old_level);		// 인터럽트 상태 복원
}

/*************************************************************
 * cmp_priority - 스레드 우선순위 비교 함수 (list_insert_ordered 전용)
 *
 * 기능:
 * - 높은 priority 값을 가진 스레드를 우선하도록 비교 (내림차순 정렬)
 * - 우선순위가 같은 경우, wakeup_ticks가 더 작은 스레드를 먼저 배치 (FIFO 보장)
 *
 * 사용 위치:
 * - ready_list 등에서 list_insert_ordered()의 비교 함수로 사용
 *************************************************************/
bool
cmp_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) 
{
	struct thread *ta = list_entry(a, struct thread, elem);		// a 요소를 thread 구조체로 변환
	struct thread *tb = list_entry(b, struct thread, elem);		// b 요소를 thread 구조체로 변환

	if (ta->priority == tb->priority)				 // 우선순위가 같은 경우 (tie-breaker)
        return ta->wakeup_ticks < tb->wakeup_ticks;  // wakeup_thicks가 빠른 스레드를 우선 배치 (FIFO)
	return ta->priority > tb->priority;				 // 우선순위가 높은 (값이 큰) 스레드를 먼저 배치
}

/*************************************************************
 * thread_set_priority - 현재 실행 중인 스레드의 우선순위 변경
 *
 * 기능:
 * - 현재 스레드의 priority 값을 new_priority로 갱신
 * - 우선순위가 낮아진 경우, ready_list에 더 높은 우선순위의 스레드가 있으면
 *   자발적으로 CPU를 양보할 수 있도록 preempt_priority() 호출
 *
 * 제약 사항:
 * - 우선순위 변경이 즉시 스케줄링 결정에 반영되어야 함 (선점형 스케줄링 보장)
 * - MLFQ 스케줄러가 활성화된 경우에는 이 함수가 무시될 수 있음
 *
 * 호출 위치 예시:
 * - 외부에서 특정 스레드의 priority를 수동 조정하고 싶을 때 사용
 *
 * 참고:
 * - 스레드 생성 시 초기 우선순위는 thread_create()에서 설정됨
 *************************************************************/
void
thread_set_priority (int new_priority) 
{
	thread_current ()->priority = new_priority;
	preempt_priority();		// 🔥 우선순위 하락 시 즉시 스케줄링 변경 여부 확인
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: Your implementation goes here */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. 
   
   ✅ TODO: priority donation을 위해 필요한 필드 초기화
     1. donations 리스트 초기화 - 우선순위 기부 내역을 관리하기 위한 리스트
     2. wait_on_lock 초기화 - 대기 중인 락의 주소를 추적하기 위한 포인터
     3. base_priority 초기화 - 원래 우선순위를 저장하는 멤버 변수
   */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF); // 인터럽트 OFF 상태인지 확인
	ASSERT (thread_current()->status == THREAD_RUNNING); // 현재 스레드가 실행중인지 확인
	while (!list_empty (&destruction_req)) { 
		struct thread *victim = // 교체될 스레드
			list_entry (list_pop_front (&destruction_req), struct thread, elem); 
		palloc_free_page(victim); // 교체될 스레드 메모리 해제
	}
	thread_current ()->status = status; 
	schedule (); // 문맥 전환
}

/*************************************************************
 * schedule - 현재 스레드를 스케줄링에서 제거하고 다음 스레드로 전환
 *
 * 기능:
 * - 현재 스레드의 상태에 따라 문맥 전환(context switch)을 수행
 * - 다음에 실행할 스레드(next_thread)를 선택하고 전환함
 * - dying 상태인 스레드는 스레드 구조체 제거를 요청 목록에 추가
 *
 * 전제 조건:
 * - 인터럽트는 반드시 비활성화된 상태여야 함 (atomicity 보장)
 * - cur->status != THREAD_RUNNING 상태여야 함 (RUNNING → READY/_BLOCKED 전환된 상태)
 *
 * 특이 사항:
 * - thread_exit()에 의해 죽은 스레드는 실제 제거가 아닌 나중에 deferred free 처리
 * - 문맥 전환은 thread_launch()를 통해 수행됨
 *************************************************************/
static void
schedule (void) 
{
	struct thread *cur = running_thread ();        // 현재 실행 중인 스레드
	struct thread *next = next_thread_to_run ();    // 다음 실행할 스레드 선택

	ASSERT (intr_get_level () == INTR_OFF);         // 인터럽트는 꺼져 있어야 함
	ASSERT (cur->status != THREAD_RUNNING);        // 현재 스레드는 더 이상 RUNNING 상태가 아니어야 함
	ASSERT (is_thread (next));                      // next가 유효한 스레드인지 확인

	next->status = THREAD_RUNNING;                  // 다음 스레드를 RUNNING 상태로 전환
	thread_ticks = 0;                               // 새 타임 슬라이스 시작

#ifdef USERPROG
	process_activate (next);                        // 사용자 프로그램이면 주소 공간 교체
#endif
	if (cur != next) {
		// 현재 스레드가 죽은 상태라면, 나중에 메모리 해제를 위해 큐에 넣음
		if (cur && cur->status == THREAD_DYING && cur != initial_thread) {
			ASSERT (cur != next);                  // dying 스레드는 당연히 next가 될 수 없음
			list_push_back (&destruction_req, &cur->elem); // 제거 요청 리스트에 추가
		}
		thread_launch (next);						// 실제 문맥 전환 수행 (레지스터/스택 등 전환)
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}

// /* ------------------ 디버깅용 리스트 출력 함수 ------------------ */
// static void
// debug_print_thread_lists(void) {
//   struct list_elem *e;

//   // printf("[LIST] ready_list: ");
//   for (e = list_begin(&ready_list); e != list_end(&ready_list); e = list_next(e)) {
//     struct thread *t = list_entry(e, struct thread, elem);
//     printf("(%s, pri=%d) ", t->name, t->priority);
//   }
//   printf("\n");

//   // printf("[LIST] sleep_list: ");
//   for (e = list_begin(&sleep_list); e != list_end(&sleep_list); e = list_next(e)) {
//     struct thread *t = list_entry(e, struct thread, elem);
//     printf("(%s, wakeup=%lld) ", t->name, t->wakeup_ticks);
//   }
//   printf("\n");
// }