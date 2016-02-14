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

struct semaphore *sem_maleF;
struct semaphore *sem_maleMM;
struct semaphore *sem_femaleM;
struct semaphore *sem_femaleMM;
struct semaphore *sem_matchmakerM;
struct semaphore *sem_matchmakerF;

void whalemating_init() {
	if(sem_maleF == NULL) {
		sem_maleF = sem_create("sem_maleF",0);
		if(sem_maleF == NULL) {
			panic("synchprobs: sem_create failed\n");
		}
	}
	if(sem_maleMM == NULL) {
		sem_maleMM = sem_create("sem_maleMM",0);
		if(sem_maleMM == NULL) {
			panic("synchprobs: sem_create failed\n");
		}
	}
	if(sem_femaleM == NULL) {
		sem_femaleM = sem_create("sem_femaleM",0);
		if(sem_femaleM == NULL) {
			panic("synchprobs: sem_create failed\n");
		}
	}
	if(sem_femaleMM == NULL) {
		sem_femaleMM = sem_create("sem_femaleMM",0);
		if(sem_femaleMM == NULL) {
			panic("synchprobs: sem_create failed\n");
		}
	}
	if(sem_matchmakerM == NULL) {
		sem_matchmakerM = sem_create("sem_matchmakerM",0);
		if(sem_matchmakerM == NULL) {
			panic("synchprobs: sem_create failed\n");
		}
	}
	if(sem_matchmakerF == NULL) {
		sem_matchmakerF = sem_create("sem_matchmakerF",0);
		if(sem_matchmakerF == NULL) {
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

	sem_destroy(sem_maleF);
	sem_destroy(sem_femaleM);
	sem_destroy(sem_matchmakerM);
	sem_destroy(sem_maleMM);
	sem_destroy(sem_femaleMM);
	sem_destroy(sem_matchmakerF);

	sem_maleF = NULL;
	sem_femaleM = NULL;
	sem_matchmakerM = NULL;
	sem_maleMM = NULL;
	sem_femaleMM = NULL;
	sem_matchmakerF = NULL;

	return;
}

void
male(uint32_t index)
{	
	male_start(index);

	V(sem_maleF);
	V(sem_maleMM);
	P(sem_femaleM);
	P(sem_matchmakerM);

	male_end(index);

}

void
female(uint32_t index)
{	female_start(index);

	V(sem_femaleM);
	V(sem_femaleMM);
	P(sem_maleF);
	P(sem_matchmakerF);

	female_end(index);

	return;
}

void
matchmaker(uint32_t index)
{	matchmaker_start(index);

	V(sem_matchmakerM);
	V(sem_matchmakerF);
	P(sem_maleMM);
	P(sem_femaleMM);

	matchmaker_end(index);

	return;
}
