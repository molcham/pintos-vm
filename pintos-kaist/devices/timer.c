#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* 8254 타이머 칩의 세부 사항은 [8254]를 참고한다. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* OS 부팅 이후 지난 타이머 틱 수. */
static int64_t ticks;

/* 한 틱당 반복할 루프 횟수.
   timer_calibrate()에서 초기화된다. */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);

/* 8254 PIT을 설정하여 초당 PIT_FREQ회 인터럽트를 발생시키고
   해당 인터럽트를 등록한다. */
void
timer_init (void) {
        /* 8254 입력 주파수를 TIMER_FREQ로 나눠 반올림한 값. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	outb (0x43, 0x34);    /* CW: counter 0, LSB then MSB, mode 2, binary. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* 짧은 지연을 위해 사용되는 loops_per_tick 값을 보정한다. */
void
timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

        /* 한 틱보다 작은 최대 2의 거듭제곱 값으로 먼저 추정한다. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

        /* 다음 8비트를 더 정밀하게 계산한다. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/*************************************************************
 * timer_ticks - OS가 부팅된 이후 경과한 총 tick 수를 반환
 *
 * 기능:
 * - 전역 변수 ticks 값을 읽어 반환
 * - 인터럽트를 비활성화하여 동기화 문제 방지
 *
 * 반환:
 * - 현재까지 누적된 timer tick 수 (int64_t)
 *
 * 주의:
 * - ticks는 인터럽트 핸들러에서 증가하므로, 읽는 동안의 정합성을 위해
 *   intr_disable()/intr_set_level() 사용
 *************************************************************/
int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable(); // 인터럽트 비활성화하여 race condition 방지
	int64_t t = ticks;                          // ticks 값 로컬 변수로 복사
	intr_set_level(old_level);                  // 인터럽트 상태 원복
	barrier();                                  // 컴파일러 최적화 방지 (메모리 장벽)
	return t;                                   // 현재까지의 tick 수 반환
}

/* 인자로 주어진 THEN 이후 경과한 틱 수를 반환한다.
   THEN은 timer_ticks()의 반환값이어야 한다. */
int64_t
timer_elapsed (int64_t then) {
	return timer_ticks () - then;
}

/*************************************************************
 * timer_sleep - 지정된 tick 수만큼 현재 스레드를 재움
 *
 * 요구사항:
 * - busy-wait 없이 정확한 시간 동안 스레드를 BLOCKED 상태로 전환
 * - 인터럽트를 활성화한 상태에서 호출해야 함
 * 
 * 기능:
 * - 현재 tick을 기준으로, 깨어날 tick을 계산하여 thread_sleep() 호출
 * - sleep_list에 현재 스레드를 추가하고 BLOCKED 상태로 전환
 * 
 * 반환:
 * - 없음 (void)
 * 
 * 주의:
 * - ticks가 0 이하일 경우 sleep을 수행하지 않음
 * - intr_get_level() == INTR_ON 상태에서만 호출 가능
 * - sleep 중인 스레드는 timer_interrupt()에 의해 주기적으로 체크됨
 * 
 * [AS-IS]
 * - busy-wait 방식 사용
 * - while 루프에서 timer_elapsed()로 경과 시간 확인 후 thread_yield() 호출
 * → RUNNING 상태 유지하며 CPU 자원 낭비
 * 
 * [TO-BE]
 * - thread_sleep(start + ticks)를 통해 BLOCKED 상태로 진입
 * - thread_awake()가 타이머 인터럽트 시점마다 sleep_list를 확인하고 깨어남
 * → 정확하고 효율적인 sleep 가능 (CPU 절약)
 *************************************************************/
void
timer_sleep (int64_t ticks) 
{
	if (ticks <= 0) return;

	int64_t wakeup_tick = timer_ticks() + ticks;
	thread_sleep(wakeup_tick);  // ✅ 절대값 기반으로 정확한 sleep 리스트 추가
	

	// ❌ 문제 있었던 이전 코드 (AS-IS)
	// int64_t start = timer_ticks();  // 현재 tick 저장
	// ASSERT (intr_get_level () == INTR_ON);  // 인터럽트 활성화 상태인지 검사

	// if (timer_elapsed(start) < ticks)
	// 	thread_sleep(start + ticks);  // 호출 타이밍에 따라 무시될 수 있음

	/* ❌ 비효율적인 busy-wait 방식 (AS-IS)
	while (timer_elapsed(start) < ticks)
		thread_yield();  // sleep_list에 등록되지 않으므로 깨어날 수 없음 */
}

/* 약 MS 밀리초 동안 실행을 중단한다. */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/* 약 US 마이크로초 동안 실행을 중단한다. */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* 약 NS 나노초 동안 실행을 중단한다. */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* 타이머 통계를 출력한다. */
void
timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/*************************************************************
 * timer_interrupt - 타이머 인터럽트 핸들러
 *
 * 요구사항:
 * - 시스템 틱 수를 증가시켜 전체 시간 흐름을 관리
 * - 현재 실행 중인 스레드의 time slice를 갱신해야 함
 * - sleep_list에 등록된 스레드 중 깨어날 시간이 도래한 경우 깨워야 함
 * 
 * 기능:
 * - 전역 변수 ticks를 1 증가시킴
 * - thread_tick()을 호출하여 현재 스레드의 time slice 소모 체크
 * - closest_tick(다음 깨어날 시각)과 비교하여 thread_awake() 호출 여부 결정
 *
 * 주의:
 * - 인터럽트 컨텍스트에서 실행되므로 thread_block()과 같은 동작은 금지
 * - closest_tick은 sleep_list에 있는 가장 이른 wakeup_tick 값을 나타냄
 * 
 * [AS-IS]
 * - 단순히 ticks 증가 및 thread_tick() 호출만 수행
 * → sleep 상태의 스레드를 깨우는 기능이 없음
 * 
 * [TO-BE]
 * - closest_tick <= ticks일 때만 thread_awake()를 호출하여,
 *   sleep_list에서 깨어날 시각이 지난 스레드를 READY 상태로 전환
 * → 정확하고 효율적인 알람 기능 제공
 *************************************************************/
static void
timer_interrupt (struct intr_frame *args UNUSED) 
{
	ticks++;						// 전체 시스템 tick 수 증가
	thread_tick ();					// 현재 running 중인 thread의 tick 처리 및 time slice 만료 검사
	if (closest_tick() <= ticks)  	// 가장 이른 wakeup_tick(closest_tick)이 현재 tick보다 작거나 같다면
		thread_awake(ticks);		// → 깨어날 시간이 도래한 스레드가 존재할 수 있으므로 thread_awake() 호출
}

/* LOOPS번 반복이 한 틱 이상 걸리면 true,
   그렇지 않으면 false를 반환한다. */
static bool
too_many_loops (unsigned loops) {
	/* Wait for a timer tick. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* Run LOOPS loops. */
	start = ticks;
	busy_wait (loops);

	/* If the tick count changed, we iterated too long. */
	barrier ();
	return start != ticks;
}

/* 간단한 루프를 LOOPS번 돌려 짧은 지연을 구현한다.

   코드 정렬에 따라 시간이 달라질 수 있으므로
   여러 곳에 인라인될 때 예측이 어렵지 않도록 NO_INLINE으로 둔다. */
static void NO_INLINE
busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/* NUM/DENOM 초 동안 잠든다. */
static void
real_time_sleep (int64_t num, int32_t denom) {
        /* NUM/DENOM 초를 타이머 틱으로 환산한 값.

           (NUM / DENOM) s
           ---------------------- = NUM * TIMER_FREQ / DENOM 틱 */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
                /* 한 틱 이상 대기해야 하므로 timer_sleep()을 사용하여
                   CPU를 다른 스레드에 양보한다. */
		timer_sleep (ticks);
	} else {
                /* 그렇지 않으면 더 정확한 지연을 위해 busy-wait를 사용한다.
                   오버플로를 피하려고 분자와 분모를 1000으로 줄인다. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}

