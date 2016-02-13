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

static struct lock *lk_male;
static struct lock *lk_female;
static struct lock *lk_matchmaker;
static struct semaphore *sem_mateon;
static struct lock *lk_mateon;
static struct cv *cv_mateon;
static struct semaphore *sem_mateoff;
static struct lock *lk_mateoff;
static struct cv *cv_mateoff;

void whalemating_init() {
	
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
	if(sem_mateon == NULL){
		sem_mateon = sem_create("sem_mateon",3);
		if(sem_mateon == NULL){
			panic("synchprobs: sem_create failed\n");
		}
	}	
	if(lk_mateon == NULL) {
		lk_mateon = lock_create("lk_mateon");
		if(lk_mateon == NULL) {
			panic("synchprobs: lock_create failed\n");
		}
	}
	if(cv_mateon == NULL) {
		cv_mateon = cv_create("cv_mateon");
		if(cv_mateon ==NULL) {
			panic("synchprobs: cv_create failed\n");
		}
	}
	if(sem_mateoff == NULL){
		sem_mateoff = sem_create("sem_mateoff",3);
		if(sem_mateoff == NULL){
			panic("synchprobs: sem_create failed\n");
		}
	}	
	if(lk_mateoff == NULL) {
		lk_mateoff = lock_create("lk_mateoff");
		if(lk_mateoff == NULL) {
			panic("synchprobs: lock_create failed\n");
		}
	}
	if(cv_mateoff == NULL) {
		cv_mateoff = cv_create("cv_mateoff");
		if(cv_mateoff ==NULL) {
			panic("synchprobs: cv_create failed\n");
		}
	}
	return;
}

/*
 * Called by the driver during teardown.
 */

void
whalemating_cleanup() {

	lock_destroy(lk_male);
	lock_destroy(lk_female);
	lock_destroy(lk_matchmaker);
	sem_destroy(sem_mateon);	
	lock_destroy(lk_mateon);
	cv_destroy(cv_mateon);
	sem_destroy(sem_mateoff);	
	lock_destroy(lk_mateoff);
	cv_destroy(cv_mateoff);
	lk_male = NULL;
	lk_female = NULL;
	lk_matchmaker = NULL;
	sem_mateon = NULL;
	lk_mateon = NULL;
	cv_mateon = NULL;
	sem_mateoff = NULL;
	lk_mateoff = NULL;
	cv_mateoff = NULL;
	
	return;
}

void
male(uint32_t index)
{	
	lock_acquire(lk_male);
	lock_acquire(lk_mateon);
	P(sem_mateon);			
	if(sem_mateon->sem_count == 0) {
		cv_broadcast(cv_mateon,lk_mateon); //broadcast that everyone is available for mating
	} 
	else {
		cv_wait(cv_mateon,lk_mateon); //wait for female and matchmaker to come for mating
	}
	male_start(index);
	V(sem_mateon);
	lock_release(lk_male);
	lock_acquire(lk_mateoff);
	P(sem_mateoff);
	lock_release(lk_mateon);
	
	lock_acquire(lk_male);
	if(sem_mateoff->sem_count == 0) {
		cv_broadcast(cv_mateoff,lk_mateoff); //broadcast that everyone has finished
	} 
	else {
		cv_wait(cv_mateoff,lk_mateoff); //wait for female and matchmaker finish
	}
	male_end(index);
	V(sem_mateoff);
	lock_release(lk_male);
	lock_release(lk_mateoff);
	
	return;
}

void
female(uint32_t index)
{
	lock_acquire(lk_female);
	lock_acquire(lk_mateon);		
	P(sem_mateon);
	if(sem_mateon->sem_count == 0) {
		cv_broadcast(cv_mateon,lk_mateon); //broadcast that everyone is available for mating
	} 
	else {
		cv_wait(cv_mateon,lk_mateon); //wait for male and matchmaker to come for mating
	}
	female_start(index);
	V(sem_mateon);
	lock_release(lk_female);
	lock_acquire(lk_mateoff);
	P(sem_mateoff);
	lock_release(lk_mateon);
	
	lock_acquire(lk_female);
	if(sem_mateoff->sem_count == 0) {
		cv_broadcast(cv_mateoff,lk_mateoff); //broadcast that everyone has finished
	} 
	else {
		cv_wait(cv_mateoff,lk_mateoff); //wait for male and matchmaker finish
	}
	female_end(index);
	V(sem_mateoff);
	lock_release(lk_female);
	lock_release(lk_mateoff);
	
	return;
}

void
matchmaker(uint32_t index)
{	
	lock_acquire(lk_matchmaker);
	lock_acquire(lk_mateon);		
	P(sem_mateon);
	if(sem_mateon->sem_count == 0) {
		cv_broadcast(cv_mateon,lk_mateon); //broadcast that everyone is available for mating
	} 
	else {
		cv_wait(cv_mateon,lk_mateon); //wait for female and male to come for mating
	}
	matchmaker_start(index);
	V(sem_mateon);
	lock_release(lk_matchmaker);
	lock_acquire(lk_mateoff);
	P(sem_mateoff);
	lock_release(lk_mateon);
	

	lock_acquire(lk_matchmaker);
	if(sem_mateoff->sem_count == 0) {
		cv_broadcast(cv_mateoff,lk_mateoff); //broadcast that everyone has finished
	} 
	else {
		cv_wait(cv_mateoff,lk_mateoff); //wait for female and male finish
	}
	matchmaker_end(index);
	V(sem_mateoff);
	lock_release(lk_matchmaker);
	lock_release(lk_mateoff);
	
	return;
}
