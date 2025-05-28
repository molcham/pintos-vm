#include "devices/intq.h"
#include <debug.h>
#include "threads/thread.h"

static int next (int pos);
static void wait (struct intq *q, struct thread **waiter);
static void signal (struct intq *q, struct thread **waiter);

/* 인터럽트 큐 Q를 초기화한다. */
void
intq_init (struct intq *q) {
	lock_init (&q->lock);
	q->not_full = q->not_empty = NULL;
	q->head = q->tail = 0;
}

/* Q가 비어 있으면 true, 아니면 false 를 반환한다. */
bool
intq_empty (const struct intq *q) {
	ASSERT (intr_get_level () == INTR_OFF);
	return q->head == q->tail;
}

/* Q가 가득 차 있으면 true, 아니면 false 를 반환한다. */
bool
intq_full (const struct intq *q) {
	ASSERT (intr_get_level () == INTR_OFF);
	return next (q->head) == q->tail;
}

/* Q에서 바이트 하나를 꺼내 반환한다.
   인터럽트 핸들러에서 호출할 경우 Q가 비어 있지 않아야 한다.
   그 외에는 Q가 비어 있으면 데이터가 채워질 때까지 잠든다. */
uint8_t
intq_getc (struct intq *q) {
	uint8_t byte;

	ASSERT (intr_get_level () == INTR_OFF);
	while (intq_empty (q)) {
		ASSERT (!intr_context ());
		lock_acquire (&q->lock);
		wait (q, &q->not_empty);
		lock_release (&q->lock);
	}

	byte = q->buf[q->tail];
	q->tail = next (q->tail);
	signal (q, &q->not_full);
	return byte;
}

/* Q의 끝에 BYTE를 추가한다.
   인터럽트 핸들러에서 호출한다면 Q가 가득 차 있지 않아야 한다.
   그 외에는 Q가 가득 차면 공간이 날 때까지 잠든다. */
void
intq_putc (struct intq *q, uint8_t byte) {
	ASSERT (intr_get_level () == INTR_OFF);
	while (intq_full (q)) {
		ASSERT (!intr_context ());
		lock_acquire (&q->lock);
		wait (q, &q->not_full);
		lock_release (&q->lock);
	}

	q->buf[q->head] = byte;
	q->head = next (q->head);
	signal (q, &q->not_empty);
}

/* POS 바로 다음 위치를 반환한다. */
static int
next (int pos) {
	return (pos + 1) % INTQ_BUFSIZE;
}

/* WAITER는 Q의 not_empty 또는 not_full 멤버 주소여야 하며
   해당 조건이 만족될 때까지 기다린다. */
static void
wait (struct intq *q UNUSED, struct thread **waiter) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT ((waiter == &q->not_empty && intq_empty (q))
			|| (waiter == &q->not_full && intq_full (q)));

	*waiter = thread_current ();
	thread_block ();
}

/* WAITER는 Q의 not_empty 또는 not_full 멤버 주소이며 조건이 참일 때 호출된다.
   대기 중인 스레드가 있다면 깨우고 그 포인터를 초기화한다. */
static void
signal (struct intq *q UNUSED, struct thread **waiter) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT ((waiter == &q->not_empty && !intq_empty (q))
			|| (waiter == &q->not_full && !intq_full (q)));

	if (*waiter != NULL) {
		thread_unblock (*waiter);
		*waiter = NULL;
	}
}
