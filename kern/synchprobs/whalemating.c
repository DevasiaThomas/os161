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
 * Driver code is in kern/tests/synchprobs.c We will
 * replace that file. This file is yours to modify as you see fit.
 *
 * You should implement your solution to the whalemating problem below.
 */

#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

/*
 * Called by the driver during initialization.
 */

static struct cv *cv_whalemating;
static struct lock *lock_whalemating;
static struct spinlock splk_whalemating;
static uint32_t c_male;
static uint32_t c_female;
static uint32_t c_matchmaker;

void whalemating_init() {
	if(cv_whalemating== NULL) {
		cv_whalemating = cv_create("cv_whalemating");
		if(cv_whalemating ==NULL) {
			panic("synchprobs: cv_create failed\n");
		}
	}
	if(lock_whalemating == NULL) {
		lock_whalemating = lock_create("lock_whalemating");
		if(lock_whalemating == NULL) {
			panic("synchprobs: lock_create failed\n");
		}
	}
	spinlock_init(&splk_whalemating);
	return;
}

/*
 * Called by the driver during teardown.
 */

void
whalemating_cleanup() {
	return;
}

void
male(uint32_t index)
{
	lock_acquire(lock_whalemating);
	spinlock_acquire(&splk_whalemating);
	c_male++;
	spinlock_release(&splk_whalemating);
	male_start(index);
	
	while(c_female == 0 && c_matchmaker == 0){
		cv_wait(cv_whalemating,lock_whalemating);
	}
	
	male_end(index);
	spinlock_acquire(&splk_whalemating);
	c_male--;
	cv_broadcast(cv_whalemating,lock_whalemating);
	spinlock_release(&splk_whalemating);
	lock_release(lock_whalemating);
	/*
	 * Implement this function by calling male_start and male_end when
	 * appropriate.
	 */
	return;
}

void
female(uint32_t index)
{
	lock_acquire(lock_whalemating);
	spinlock_acquire(&splk_whalemating);
	c_female++;
	spinlock_release(&splk_whalemating);
	female_start(index);	

	while(c_male == 0 && c_matchmaker == 0){
		cv_wait(cv_whalemating,lock_whalemating);
	}
	
	female_end(index);
	spinlock_acquire(&splk_whalemating);
	c_female--;
	cv_broadcast(cv_whalemating,lock_whalemating);
	spinlock_release(&splk_whalemating);
	lock_release(lock_whalemating);
	
	/*
	 * Implement this function by calling female_start and female_end when
	 * appropriate.
	 */
	return;
}

void
matchmaker(uint32_t index)
{
	lock_acquire(lock_whalemating);

	spinlock_acquire(&splk_whalemating);
	c_matchmaker++;

	spinlock_release(&splk_whalemating);
	matchmaker_start(index);	

	while(c_male == 0 && c_female == 0){
		cv_wait(cv_whalemating,lock_whalemating);
	}
	
	matchmaker_end(index);
	spinlock_acquire(&splk_whalemating);
	c_matchmaker--;
	cv_broadcast(cv_whalemating,lock_whalemating);
	spinlock_release(&splk_whalemating);
	lock_release(lock_whalemating);
	/*
	 * Implement this function by calling matchmaker_start and matchmaker_end
	 * when appropriate.
	 */
	return;
}
