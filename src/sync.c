#include "sync.h"

#include <string.h>

static int gate_init(struct gate *gate)
{
	int err;

	memset(gate, 0, sizeof(*gate));

	err = pthread_mutex_init(&gate->lock, NULL);
	if (err)
		return err;
	err = pthread_cond_init(&gate->cond, NULL);
	if (err) {
		pthread_mutex_destroy(&gate->lock);
		return err;
	}

	return 0;
}

static void gate_destroy(struct gate *gate)
{
	pthread_cond_destroy(&gate->cond);
	pthread_mutex_destroy(&gate->lock);
}

void gate_signal(struct gate *gate)
{
	pthread_mutex_lock(&gate->lock);
	gate->signaled = true;
	pthread_cond_broadcast(&gate->cond);
	pthread_mutex_unlock(&gate->lock);
}

void gate_stop(struct gate *gate)
{
	pthread_mutex_lock(&gate->lock);
	gate->stop = true;
	pthread_cond_broadcast(&gate->cond);
	pthread_mutex_unlock(&gate->lock);
}

int gate_wait(struct gate *gate)
{
	bool stop;
	bool signaled;

	pthread_mutex_lock(&gate->lock);
	while (!gate->signaled && !gate->stop)
		pthread_cond_wait(&gate->cond, &gate->lock);
	signaled = gate->signaled;
	stop = gate->stop;
	pthread_mutex_unlock(&gate->lock);

	return (!stop && signaled) ? 0 : -1;
}

int race_sync_init(struct race_sync *sync)
{
	if (gate_init(&sync->start) != 0)
		return -1;
	if (gate_init(&sync->first_frame_done) != 0)
		goto out_start;

	return 0;

out_start:
	gate_destroy(&sync->start);
	return -1;
}

void race_sync_stop(struct race_sync *sync)
{
	gate_stop(&sync->start);
	gate_stop(&sync->first_frame_done);
}

void race_sync_destroy(struct race_sync *sync)
{
	gate_destroy(&sync->first_frame_done);
	gate_destroy(&sync->start);
}

void join_race_threads(pthread_t sender, bool sender_started,
		       pthread_t closer, bool closer_started)
{
	if (sender_started)
		pthread_join(sender, NULL);
	if (closer_started)
		pthread_join(closer, NULL);
}
