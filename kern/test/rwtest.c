/*
 * All the contents of this file are overwritten during automated
 * testing. Please consider this before changing anything in this file.
 */

#include <types.h>
#include <lib.h>
#include <clock.h>
#include <thread.h>
#include <synch.h>
#include <test.h>
#include <kern/secret.h>
#include <spinlock.h>

#define NTHREADS 32
#define CREATELOOPS 8

static struct semaphore *donesem = NULL;
static struct rwlock *rwlock = NULL;
struct spinlock status_lock;
struct spinlock splk;
static bool test_status = FAIL;

static volatile unsigned long testval1;
static volatile unsigned long testval2;

static
void
writer_thread(void *junk, unsigned long num)
{
	(void)junk;
	//kprintf_n("writer thread: %lu waiting to write", num);

	rwlock_acquire_write(rwlock);
	kprintf_n("writer thread: %lu, %lu writers currently writing\n",num,++testval2);
	spinlock_acquire(&status_lock);
	if(testval1 != 0){
		kprintf_n("rwtest failed: writer acquired the lock while readers had it\n");
		test_status = FAIL;
	}
	if(testval2 != 1){
		kprintf_n("rwtest failed: writer acquired the lock while a writer had it\n");
		test_status = FAIL;
	}
	spinlock_release(&status_lock);
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

int rwtest(int nargs, char **args) {
	(void)nargs;
	(void)args;

	unsigned i;
	testval1 = 0;
	testval2 = 0;
	int result;	
	test_status = SUCCESS;
	if(rwlock == NULL){
		rwlock = rwlock_create("reader_writer_lock");
		if(rwlock == NULL){
			panic("synchtest: rwlock_create failed\n");	
		}
	}
	if(donesem == NULL){
		donesem = sem_create("donesem",0);
		if(donesem == NULL){
			panic("synchtest: sem_create failed\n");	
		}
	}
	spinlock_init(&splk);
	spinlock_init(&status_lock);
	
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
	sem_destroy(donesem);
	spinlock_cleanup(&splk);
	donesem = NULL;
	rwlock = NULL;	

	kprintf_n("rwtest done\n");
	success(test_status, SECRET, "rwt1");

	//kprintf_n("rwt1 unimplemented\n");
	//success(FAIL, SECRET, "rwt1");

	return 0;
}

int rwtest2(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("rwt2 unimplemented\n");
	success(FAIL, SECRET, "rwt2");

	return 0;
}

static
void
rwt3thread(void *junk, unsigned long index)
{
	(void)junk;

	switch(index) {
		case 0:
			rwlock_acquire_read(rwlock);
			P(donesem);
			break;
		case 1:
			ksecprintf(SECRET, "should panic...", "rwt3");
			rwlock_release_read(rwlock);
			V(donesem);
			break;
		case 2:
			rwlock_acquire_write(rwlock);
			P(donesem);
			break;
		case 3:
			ksecprintf(SECRET, "should panic...", "rwt4");
			V(donesem);
			break;
	}
	//should not get here on success
	V(donesem);	
	success(FAIL, SECRET, "rwt3");
	rwlock_destroy(rwlock);
	rwlock = NULL;

}

int rwtest3(int nargs, char **args) {
	(void)nargs;
	(void)args;
	
	int result;
	if(rwlock == NULL){
		rwlock = rwlock_create("reader_writer_lock");
		if(rwlock == NULL){
			panic("synchtest: rwlock_create failed\n");	
		}
	}
	if(donesem == NULL){
		donesem = sem_create("donesem",0);
		if(donesem == NULL){
			panic("synchtest: sem_create failed\n");	
		}
	}
	kprintf_n("Starting rwt3...\n");
	kprintf_n("(This test panics on success!)\n");
	/*for (i=0; i<CREATELOOPS; i++) {
		rwlock = rwlock_create("rwlock");
		if (rwlock == NULL) {
			panic("rwt3: lock_create failed\n");
		}
		if (i != CREATELOOPS - 1) {
			rwlock_destroy(rwlock);
		}
	}
*/
	result = thread_fork("writer", NULL, rwt3thread, NULL, 0);
	result = thread_fork("writer", NULL, rwt3thread, NULL, 1);
	if (result) {
		panic("rwtest: thread_fork failed: (%s)\n", strerror(result));
	}//ksecprintf(SECRET, "Should panic...", "rwt3");
	//rwlock_release_read(rwlock);

	/* Should not get here on success. */
	P(donesem);
	success(FAIL, SECRET, "rwt3");

	rwlock_destroy(rwlock);
	rwlock = NULL;

	return 0;

//	kprintf_n("rwt3 unimplemented\n");
//	success(FAIL, SECRET, "rwt3");

	return 0;
}

int rwtest4(int nargs, char **args) {
	(void)nargs;
	(void)args;
	int result;
	if(rwlock == NULL){
		rwlock = rwlock_create("reader_writer_lock");
		if(rwlock == NULL){
			panic("synchtest: rwlock_create failed\n");	
		}
	}
	if(donesem == NULL){
		donesem = sem_create("donesem",0);
		if(donesem == NULL){
			panic("synchtest: sem_create failed\n");	
		}
	}
	kprintf_n("Starting rwt4...\n");
	kprintf_n("(This test panics on success!)\n");
/*	for (i=0; i<CREATELOOPS; i++) {
		rwlock = rwlock_create("rwlock");
		if (rwlock == NULL) {
			panic("rwt4: rwlock_create failed\n");
		}
		if (i != CREATELOOPS - 1) {
			rwlock_destroy(rwlock);
		}
	}
*/
	result = thread_fork("writer", NULL, rwt3thread, NULL, 2);
	result = thread_fork("writer", NULL, rwt3thread, NULL, 3);
	if (result) {
		panic("rwtest: thread_fork failed: (%s)\n", strerror(result));
	}

//	ksecprintf(SECRET, "Should panic...", "lt2");
//	rwlock_acquire_read(rwlock);
//	rwlock_destroy(rwlock);

	/* Should not get here on success. */
	P(donesem);
	success(FAIL, SECRET, "rwt4");

	rwlock_destroy(rwlock);
	rwlock = NULL;

	return 0;

	//kprintf_n("rwt4 unimplemented\n");
	//success(FAIL, SECRET, "rwt4");

	return 0;
}

int rwtest5(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("rwt5 unimplemented\n");
	success(FAIL, SECRET, "rwt5");

	return 0;
}
