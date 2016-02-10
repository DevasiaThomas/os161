/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Synchronization test code.
 */

#include <types.h>
#include <lib.h>
#include <clock.h>
#include <thread.h>
#include <synch.h>
#include <test.h>
#include <kern/secret.h>
#include <spinlock.h>

#define NSEMLOOPS     63
#define NLOCKLOOPS    120
#define NCVLOOPS      5
#define NTHREADS      32

static volatile unsigned long testval1;
static volatile unsigned long testval2;
static volatile unsigned long testval3;
static volatile int32_t testval4;

static struct semaphore *testsem;
static struct lock *testlock;
static struct cv *testcv;
static struct semaphore *donesem;
static struct spinlock splk;

struct spinlock status_lock;
static bool test_status;

static unsigned long semtest_current;
static struct rwlock *rwlock;

static
void
inititems(void)
{
	if (testsem==NULL) {
		testsem = sem_create("testsem", 2);
		if (testsem == NULL) {
			panic("synchtest: sem_create failed\n");
		}
	}
	if (testlock==NULL) {
		testlock = lock_create("testlock");
		if (testlock == NULL) {
			panic("synchtest: lock_create failed\n");
		}
	}
	if (testcv==NULL) {
		testcv = cv_create("testlock");
		if (testcv == NULL) {
			panic("synchtest: cv_create failed\n");
		}
	}
	if (donesem==NULL) {
		donesem = sem_create("donesem", 0);
		if (donesem == NULL) {
			panic("synchtest: sem_create failed\n");
		}
	}
	if(rwlock == NULL){
		rwlock = rwlock_create("reader_writer_lock");
		if(rwlock == NULL){
			panic("synchtest: rwlock_create failed\n");	
		}
	}
	spinlock_init(&splk);
	spinlock_init(&status_lock);
}


static
void
semtestthread(void *junk, unsigned long num)
{
	int i;
	(void)junk;

	/*
	 * Only one of these should print at a time.
	 */
	random_yielder(4);
	P(testsem);
	semtest_current = num;
	kprintf_n("Thread %2lu: ", num);

	for (i=0; i<NSEMLOOPS; i++) {
		kprintf_n("%c", (int)num+64);
		random_yielder(4);
		if (semtest_current != num) {
			spinlock_acquire(&status_lock);
			test_status = FAIL;
			spinlock_release(&status_lock);
		}
	}

	kprintf_n("\n");
	V(donesem);
}

int
semtest(int nargs, char **args)
{
	int i, result;

	(void)nargs;
	(void)args;

	inititems();
	test_status = FAIL;
	kprintf_n("Starting semaphore test...\n");
	kprintf_n("If this hangs, it's broken: ");
	P(testsem);
	P(testsem);
	test_status = SUCCESS;
	kprintf_n("ok\n");

	for (i=0; i<NTHREADS; i++) {
		result = thread_fork("semtest", NULL, semtestthread, NULL, i);
		if (result) {
			panic("semtest: thread_fork failed: %s\n",
			      strerror(result));
		}
	}

	for (i=0; i<NTHREADS; i++) {
		V(testsem);
		P(donesem);
	}

	/* so we can run it again */
	V(testsem);
	V(testsem);
	
	kprintf_n("Semaphore test done.\n");
	success(test_status, SECRET, "sy1");

	return 0;
}

static
void
fail(unsigned long num, const char *msg)
{
	kprintf_n("thread %lu: Mismatch on %s\n", num, msg);
	kprintf_n("Test failed\n");

	lock_release(testlock);

	spinlock_acquire(&status_lock);
	test_status = FAIL;
	spinlock_release(&status_lock);

	V(donesem);
	thread_exit();
}

static
void
locktestthread(void *junk, unsigned long num)
{
	int i;
	(void)junk;

	for (i=0; i<NLOCKLOOPS; i++) {
		lock_acquire(testlock);
		random_yielder(4);

		testval1 = num;
		testval2 = num*num;
		testval3 = num%3;

		if (testval2 != testval1*testval1) {
			fail(num, "testval2/testval1");
		}
		random_yielder(4);

		if (testval2%3 != (testval3*testval3)%3) {
			fail(num, "testval2/testval3");
		}
		random_yielder(4);

		if (testval3 != testval1%3) {
			fail(num, "testval3/testval1");
		}
		random_yielder(4);

		if (testval1 != num) {
			fail(num, "testval1/num");
		}
		random_yielder(4);

		if (testval2 != num*num) {
			fail(num, "testval2/num");
		}
		random_yielder(4);

		if (testval3 != num%3) {
			fail(num, "testval3/num");
		}
		random_yielder(4);

		lock_release(testlock);
	}
	V(donesem);
}


int
locktest(int nargs, char **args)
{
	int i, result;

	(void)nargs;
	(void)args;

	inititems();
	test_status = SUCCESS;
	kprintf_n("Starting lock test...\n");

	for (i=0; i<NTHREADS; i++) {
		result = thread_fork("synchtest", NULL, locktestthread, NULL, i);
		if (result) {
			panic("locktest: thread_fork failed: %s\n", strerror(result));
		}
	}
	for (i=0; i<NTHREADS; i++) {
		P(donesem);
	}
	
	kprintf_n("Lock test done.\n");
	success(test_status, SECRET, "sy2");

	return 0;
}

static
void
cvtestthread(void *junk, unsigned long num)
{
	int i;
	volatile int j;
	struct timespec ts1, ts2;

	(void)junk;

	for (i=0; i<NCVLOOPS; i++) {
		lock_acquire(testlock);
		while (testval1 != num) {
			testval2 = 0;
			random_yielder(4);
			gettime(&ts1);
			cv_wait(testcv, testlock);
			gettime(&ts2);
			random_yielder(4);

			/* ts2 -= ts1 */
			timespec_sub(&ts2, &ts1, &ts2);

			/* Require at least 2000 cpu cycles (we're 25mhz) */
			if (ts2.tv_sec == 0 && ts2.tv_nsec < 40*2000) {
				kprintf_n("cv_wait took only %u ns\n", ts2.tv_nsec);
				kprintf_n("That's too fast... you must be " "busy-looping\n");
				spinlock_acquire(&status_lock);
				test_status = FAIL;
				spinlock_release(&status_lock);
				V(donesem);
				thread_exit();
			}

			testval2 = 0xFFFFFFFF;
		}
		testval2 = num;

		/*
		 * loop a little while to make sure we can measure the
		 * time waiting on the cv.
		 */
		for (j=0; j<3000; j++);

		random_yielder(4);
		cv_broadcast(testcv, testlock);
		random_yielder(4);
		
		spinlock_acquire(&status_lock);
		if (testval1 != testval2) {
			test_status = FAIL;
		}
		spinlock_release(&status_lock);

		kprintf_n("Thread %lu\n", testval2);
		testval1 = (testval1 + NTHREADS - 1) % NTHREADS;
		lock_release(testlock);
	}
	V(donesem);
}

int
cvtest(int nargs, char **args)
{
	int i, result;

	(void)nargs;
	(void)args;

	inititems();
	kprintf_n("Starting CV test...\n");
	kprintf_n("Threads should print out in reverse order.\n");

	testval1 = NTHREADS-1;

	for (i=0; i<NTHREADS; i++) {
		result = thread_fork("synchtest", NULL, cvtestthread, NULL, (long unsigned) i);
		if (result) {
			panic("cvtest: thread_fork failed: %s\n", strerror(result));
		}
	}
	for (i=0; i<NTHREADS; i++) {
		P(donesem);
	}
	
	kprintf_n("CV test done\n");
	success(test_status, SECRET, "sy3");

	return 0;
}

////////////////////////////////////////////////////////////

/*
 * Try to find out if going to sleep is really atomic.
 *
 * What we'll do is rotate through NCVS lock/CV pairs, with one thread
 * sleeping and the other waking it up. If we miss a wakeup, the sleep
 * thread won't go around enough times.
 */

#define NCVS 250
#define NLOOPS 40
static struct cv *testcvs[NCVS];
static struct lock *testlocks[NCVS];
static struct semaphore *gatesem;
static struct semaphore *exitsem;

static
void
sleepthread(void *junk1, unsigned long junk2)
{
	unsigned i, j;

	(void)junk1;
	(void)junk2;
	
	random_yielder(4);

	for (j=0; j<NLOOPS; j++) {
		for (i=0; i<NCVS; i++) {
			lock_acquire(testlocks[i]);
			random_yielder(4);
			V(gatesem);
			random_yielder(4);
			spinlock_acquire(&status_lock);
			testval4++;
			spinlock_release(&status_lock);
			cv_wait(testcvs[i], testlocks[i]);
			random_yielder(4);
			lock_release(testlocks[i]);
		}
		kprintf_n("sleepthread: %u\n", j);
	}
	V(exitsem);
}

static
void
wakethread(void *junk1, unsigned long junk2)
{
	unsigned i, j;

	(void)junk1;
	(void)junk2;

	random_yielder(4);

	for (j=0; j<NLOOPS; j++) {
		for (i=0; i<NCVS; i++) {
			random_yielder(4);
			P(gatesem);
			random_yielder(4);
			lock_acquire(testlocks[i]);
			random_yielder(4);
			spinlock_acquire(&status_lock);
			testval4--;
			if (testval4 != 0) {
				test_status = FAIL;
			}
			spinlock_release(&status_lock);
			cv_signal(testcvs[i], testlocks[i]);
			random_yielder(4);
			lock_release(testlocks[i]);
		}
		kprintf_n("wakethread: %u\n", j);
	}
	V(exitsem);
}

int
cvtest2(int nargs, char **args)
{
	unsigned i;
	int result;

	(void)nargs;
	(void)args;
	
	inititems();
	test_status = SUCCESS;

	for (i=0; i<NCVS; i++) {
		testlocks[i] = lock_create("cvtest2 lock");
		testcvs[i] = cv_create("cvtest2 cv");
	}
	gatesem = sem_create("gatesem", 0);
	exitsem = sem_create("exitsem", 0);

	kprintf_n("cvtest2...\n");

	result = thread_fork("cvtest2", NULL, sleepthread, NULL, 0);
	if (result) {
		panic("cvtest2: thread_fork failed\n");
	}
	result = thread_fork("cvtest2", NULL, wakethread, NULL, 0);
	if (result) {
		panic("cvtest2: thread_fork failed\n");
	}

	P(exitsem);
	P(exitsem);

	sem_destroy(exitsem);
	sem_destroy(gatesem);
	exitsem = gatesem = NULL;
	for (i=0; i<NCVS; i++) {
		lock_destroy(testlocks[i]);
		cv_destroy(testcvs[i]);
		testlocks[i] = NULL;
		testcvs[i] = NULL;
	}

	kprintf_n("cvtest2 done\n");
	success(test_status, SECRET, "sy4");

	return 0;
}

/*
 * Complete this for ASST1.
 */

#define NREADER 128
#define NWRITER 64

static
void
writer_thread(void *junk, unsigned long num)
{
	(void)junk;
	//kprintf_n("writer thread: %lu waiting to write", num);

	rwlock_acquire_write(rwlock);
	spinlock_acquire(&status_lock);
	if(testval1 != 0){
		kprintf_n("rwtest failed: writer acquired the lock while readers had it\n");
		test_status = FAIL;
	}
	if(testval2 !=0){
		kprintf_n("rwtest failed: writer acquired the lock while a writer had it\n");
		test_status = FAIL;
	}
	spinlock_release(&status_lock);
	kprintf_n("writer thread: %lu, %lu writers currently writing\n",num,++testval2);
	kprintf_n("writer thread: %lu exiting, %lu writers now writing\n",num,--testval2);
	rwlock_release_write(rwlock);
	V(donesem);
}

static
void
reader_thread(void *junk, unsigned long num)
{
	(void)junk;

	rwlock_acquire_read(rwlock);
	spinlock_acquire(&splk);
	spinlock_acquire(&status_lock);
	if(testval2 != 0){
		kprintf_n("rwtest failed: reader acquired the lock while a writer had it\n");
		test_status = FAIL;
	}
	spinlock_release(&status_lock);
	kprintf_n("reader thread: %lu, %lu readers currently reading\n",num,++testval1);
	spinlock_release(&splk);
	spinlock_acquire(&splk);
	kprintf_n("reader thread: %lu exiting, %lu readers now reading\n",num,--testval1);
	spinlock_release(&splk);
	rwlock_release_read(rwlock);
	V(donesem);
}

int 
rwtest(int nargs, char **args) 
{
	
	(void) nargs;
	(void) args;
	
	unsigned i;
	testval1 = 0;
	testval2 = 0;
	int result;	
	test_status = SUCCESS;
	inititems();
	kprintf_n("rwtest started\n");
	int c_reader=0,c_writer=0;
	
	for(i = 0; i < NTHREADS*5; i++) {
		switch (i % 3) {
			case 1:
				c_writer++;
				result = thread_fork("writer", NULL, writer_thread, NULL, i);
				break;
			default:
				c_reader++;
				result = thread_fork("reader", NULL, reader_thread, NULL, i);
				break;
		}
		if (result) {
			panic("rwtest: thread_fork failed: (%s)\n", strerror(result));
		}
	}
	
	for(i = 0; i < NTHREADS*5; i++) {
		P(donesem);
	}
	rwlock_destroy(rwlock);	

	kprintf_n("rwtest done\n");
	success(test_status, SECRET, "sy5");

	return 0;
}
