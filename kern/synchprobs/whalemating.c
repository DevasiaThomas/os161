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
static struct lock *lk_whalemating;
static struct lock *lk_male;
static struct lock *lk_female;
static struct lock *lk_matchmaker;
static struct semaphore *sem_whalemating;
static struct semaphore *donesem;

void whalemating_init() {
	if(cv_whalemating == NULL) {
		cv_whalemating = cv_create("cv_whalemating");
		if(cv_whalemating ==NULL) {
			panic("synchprobs: cv_create failed\n");
		}
	}
	if(lk_whalemating == NULL) {
		lk_whalemating = lock_create("lk_whalemating");
		if(lk_whalemating == NULL) {
			panic("synchprobs: lock_create failed\n");
		}
	}
	if(lk_male == NULL) {
		lk_male = lock_create("lk_male");
		if(lk_male == NULL) {
			panic("synchprobs: lock_create failed\n");
		}
	}
	if(lk_female == NULL) {
		lk_female = lock_create("lk_female");
		if(lk_female == NULL) {
			panic("synchprobs: lock_create failed\n");
		}
	}
	if(lk_matchmaker == NULL) {
		lk_matchmaker = lock_create("lk_matchmaker");
		if(lk_matchmaker == NULL) {
			panic("synchprobs: lock_create failed\n");
		}
	}
	if(sem_whalemating == NULL){
		sem_whalemating = sem_create("sem_whalemating",3);
		if(sem_whalemating == NULL){
			panic("synchprobs: sem_create failed\n");
		}
	}	
	if(donesem == NULL) {
		donesem = sem_create("donesem",3);
		if(donesem == NULL) {
			panic("synchprobs: sem_create failed\n");
		}
	}
	
	return;
}

/*
 * Called by the driver during teardown.
 */

void
whalemating_cleanup() {

	cv_destroy(cv_whalemating);
	lock_destroy(lk_whalemating);
	lock_destroy(lk_male);
	lock_destroy(lk_female);
	lock_destroy(lk_matchmaker);
	sem_destroy(sem_whalemating);
	cv_whalemating = NULL;
	lk_whalemating = NULL;
	lk_male = NULL;
	lk_female = NULL;
	lk_matchmaker = NULL;
	sem_whalemating = NULL;
	donesem = NULL;

	return;
}

void
male(uint32_t index)
{	
	male_start(index);
	lock_acquire(lk_male);	
	P(sem_whalemating);
	lock_acquire(lk_whalemating);
	if(sem_whalemating->sem_count == 0) {
		cv_broadcast(cv_whalemating,lk_whalemating); //broadcast that everyone is available for mating
	} 
	else {
		cv_wait(cv_whalemating,lk_whalemating); //wait for female and matchmaker to come for mating
	}
	lock_release(lk_whalemating);
	// mating started
	P(donesem);
	// mating finished 
	lock_acquire(lk_whalemating);
	if(donesem->sem_count == 0) {
		cv_broadcast(cv_whalemating,lk_whalemating); //broadcast that mating is finished
	}
	else {
		cv_wait(cv_whalemating,lk_whalemating); //wait for female and matchmaker to complete
	}
	V(donesem);
	V(sem_whalemating);
	male_end(index);
	lock_release(lk_whalemating);
	lock_release(lk_male);
	
	return;
}

void
female(uint32_t index)
{
	female_start(index);
	lock_acquire(lk_female);
	P(sem_whalemating);
	lock_acquire(lk_whalemating);
	if(sem_whalemating->sem_count == 0) {
		cv_broadcast(cv_whalemating,lk_whalemating); //broadcast that everyone is available for mating
	} 
	else {
		cv_wait(cv_whalemating,lk_whalemating); //wait for male and matchmaker to come for mating
	}
	lock_release(lk_whalemating);
	// mating started
	P(donesem);
	// mating finished 
	lock_acquire(lk_whalemating);
	if(donesem->sem_count == 0) {
		cv_broadcast(cv_whalemating,lk_whalemating); //broadcast that mating is finished
	}
	else {
		cv_wait(cv_whalemating,lk_whalemating); //wait for male and matchmaker to complete
	}
	V(donesem);
	V(sem_whalemating);
	female_end(index);
	lock_release(lk_whalemating);
	lock_release(lk_female);	
	
	return;
}

void
matchmaker(uint32_t index)
{	
	matchmaker_start(index);
	lock_acquire(lk_matchmaker);
	P(sem_whalemating);
	lock_acquire(lk_whalemating);
	if(sem_whalemating->sem_count == 0) {
		cv_broadcast(cv_whalemating,lk_whalemating); //broadcast that everyone is available for mating
	} 
	else {
		cv_wait(cv_whalemating,lk_whalemating); //wait for male and female to come for mating
	}
	lock_release(lk_whalemating);
	// mating started
	P(donesem);
	// mating finished 
	lock_acquire(lk_whalemating);
	if(donesem->sem_count == 0) {
		cv_broadcast(cv_whalemating,lk_whalemating); //broadcast that mating is finished
	}
	else {
		cv_wait(cv_whalemating,lk_whalemating); //wait for male and female to complete
	}
	V(donesem);
	V(sem_whalemating);
	matchmaker_end(index);
	lock_release(lk_whalemating);
	lock_release(lk_matchmaker);

	return;
}

