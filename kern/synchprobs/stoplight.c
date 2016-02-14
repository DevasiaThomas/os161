/*
 * Copyright (c) 2001, 2002, 2009
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
 * Driver code is in kern/tests/synchprobs.c We will replace that file. This
 * file is yours to modify as you see fit.
 *
 * You should implement your solution to the stoplight problem below. The
 * quadrant and direction mappings for reference: (although the problem is, of
 * course, stable under rotation)
 *
 *   |0 |
 * -     --
 *    01  1
 * 3  32
 * --    --
 *   | 2|
 *
 * As way to think about it, assuming cars drive on the right: a car entering
 * the intersection from direction X will enter intersection quadrant X first.
 * The semantics of the problem are that once a car enters any quadrant it has
 * to be somewhere in the intersection until it call leaveIntersection(),
 * which it should call while in the final quadrant.
 *
 * As an example, let's say a car approaches the intersection and needs to
 * pass through quadrants 0, 3 and 2. Once you call inQuadrant(0), the car is
 * considered in quadrant 0 until you call inQuadrant(3). After you call
 * inQuadrant(2), the car is considered in quadrant 2 until you call
 * leaveIntersection().
 *
 * You will probably want to write some helper functions to assist with the
 * mappings. Modular arithmetic can help, e.g. a car passing straight through
 * the intersection entering from direction X will leave to direction (X + 2)
 * % 4 and pass through quadrants X and (X + 3) % 4.  Boo-yah.
 *
 * Your solutions below should call the inQuadrant() and leaveIntersection()
 * functions in synchprobs.c to record their progress.
 */

#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

static struct lock *lockm;
static struct lock *lockq0;
static struct lock *lockq1;
static struct lock *lockq2;
static struct lock *lockq3;


/*
 * Called by the driver during initialization.
 */

void
stoplight_init() {
	//Creating locks for each quadrant
	if (lockm==NULL) {
		lockm = lock_create("lockqmain");
		if (lockm == NULL) {
			panic("stoplight: lockmain_create failed\n");
		}
	}	
	if (lockq0==NULL) {
		lockq0 = lock_create("lockq0");
		if (lockq0 == NULL) {
			panic("stoplight: lockq0_create failed\n");
		}
	}
	if (lockq1==NULL) {
		lockq1 = lock_create("lockq1");
		if (lockq1 == NULL) {
			panic("stoplight: lockq1_create failed\n");
		}
	}
	if (lockq2==NULL) {
		lockq2 = lock_create("lockq2");
		if (lockq2 == NULL) {
			panic("stoplight: lockq2_create failed\n");
		}
	}
	if (lockq3==NULL) {
		lockq3 = lock_create("lockq3");
		if (lockq3 == NULL) {
			panic("stoplight: lockq3_create failed\n");
		}
	}
	return;
}

/*
 * Called by the driver during teardown.
 */

void stoplight_cleanup() {
	lock_destroy(lockq0);
	lock_destroy(lockq1);
	lock_destroy(lockq2);
	lock_destroy(lockq3);
	lock_destroy(lockm);
	lockq0 = NULL;
	lockq1 = NULL;
	lockq2 = NULL;
	lockq3 = NULL;
	lockm = NULL;
	return;
}

void
turnright(uint32_t direction, uint32_t index)
{
	//(void)direction;
	//(void)index;
	/*
	 * Implement this function.
	 */
	lock_acquire(lockm);
	switch(direction){
	case 0:
		lock_acquire(lockq0);
		lock_release(lockm);
		inQuadrant(0,index);
		leaveIntersection(index);
		lock_release(lockq0);
		break;
	case 1:	
		lock_acquire(lockq1);
		lock_release(lockm);
		inQuadrant(1,index);
		leaveIntersection(index);
		lock_release(lockq1);
		break;
	case 2:	
		lock_acquire(lockq2);
		lock_release(lockm);
		inQuadrant(2,index);
		leaveIntersection(index);
		lock_release(lockq2);
		break;
	case 3:	
		lock_acquire(lockq3);
		lock_release(lockm);
		inQuadrant(3,index);
		leaveIntersection(index);
		lock_release(lockq3);
		break;
	default: panic("stoplight: wrong direction while turning right\n");
		lock_release(lockm);
		break;
	}
	return;
}
void
gostraight(uint32_t direction, uint32_t index)
{
	//(void)direction;
	//(void)index;
	/*
	 * Implement this function.
	 */
	lock_acquire(lockm);
	switch(direction){
	case 0:
		lock_acquire(lockq0);
		lock_acquire(lockq3);
		lock_release(lockm);
		inQuadrant(0,index);
		inQuadrant(3,index);
		lock_release(lockq0);
		leaveIntersection(index);
		lock_release(lockq3);
		break;
	case 1:	
		lock_acquire(lockq1);
		lock_acquire(lockq0);
		lock_release(lockm);
		inQuadrant(1,index);
		inQuadrant(0,index);
		lock_release(lockq1);
		leaveIntersection(index);
		lock_release(lockq0);
		break;
	case 2:	
		lock_acquire(lockq2);
		lock_acquire(lockq1);
		lock_release(lockm);
		inQuadrant(2,index);
		inQuadrant(1,index);
		lock_release(lockq2);
		leaveIntersection(index);
		lock_release(lockq1);
		break;
	case 3:	
		lock_acquire(lockq3);
		lock_acquire(lockq2);
		lock_release(lockm);
		inQuadrant(3,index);
		inQuadrant(2,index);
		lock_release(lockq3);
		leaveIntersection(index);
		lock_release(lockq2);
		break;
	default: panic("stoplight: wrong direction while going straight\n");
		lock_release(lockm);
		break;
	}
	return;
}
void
turnleft(uint32_t direction, uint32_t index)
{
	//(void)direction;
	//(void)index;
	/*
	 * Implement this function.
	 */
	lock_acquire(lockm);
	switch(direction){
	case 0:
		lock_acquire(lockq0);
		lock_acquire(lockq3);
		lock_acquire(lockq2);
		lock_release(lockm);
		inQuadrant(0,index);
		inQuadrant(3,index);
		lock_release(lockq0);
		inQuadrant(2,index);
		lock_release(lockq3);
		leaveIntersection(index);
		lock_release(lockq2);
		break;
	case 1:	
		lock_acquire(lockq1);
		lock_acquire(lockq0);
		lock_acquire(lockq3);
		lock_release(lockm);
		inQuadrant(1,index);
		inQuadrant(0,index);
		lock_release(lockq1);
		inQuadrant(3,index);
		lock_release(lockq0);
		leaveIntersection(index);
		lock_release(lockq3);
		break;
	case 2:	
		lock_acquire(lockq2);
		lock_acquire(lockq1);
		lock_acquire(lockq0);
		lock_release(lockm);
		inQuadrant(2,index);
		inQuadrant(1,index);
		lock_release(lockq2);
		inQuadrant(0,index);
		lock_release(lockq1);
		leaveIntersection(index);
		lock_release(lockq0);
		break;
	case 3:	
		lock_acquire(lockq3);
		lock_acquire(lockq2);
		lock_acquire(lockq1);
		lock_release(lockm);
		inQuadrant(3,index);
		inQuadrant(2,index);
		lock_release(lockq3);
		inQuadrant(1,index);
		lock_release(lockq2);
		leaveIntersection(index);
		lock_release(lockq1);
		break;
	default: panic("stoplight: wrong direction while turning left\n");
		lock_release(lockm);
		break;
	}
	return;
}
