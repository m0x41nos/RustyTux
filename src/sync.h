#ifndef ESPINTCP_SYNC_H
#define ESPINTCP_SYNC_H

#include <stdbool.h>
#include <pthread.h>

struct gate {
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool signaled;
	bool stop;
};

struct race_sync {
	struct gate start;
	struct gate first_frame_done;
};

void gate_signal(struct gate *gate);
void gate_stop(struct gate *gate);
int gate_wait(struct gate *gate);
int race_sync_init(struct race_sync *sync);
void race_sync_stop(struct race_sync *sync);
void race_sync_destroy(struct race_sync *sync);
void join_race_threads(pthread_t sender, bool sender_started,
		       pthread_t closer, bool closer_started);

#endif
