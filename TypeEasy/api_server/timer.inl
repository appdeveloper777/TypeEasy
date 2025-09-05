/* Copyright (c) 2013-2017 the Civetweb developers
 * Copyright (c) 2004-2013 Sergey Lyubka
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


/*
 * This file contains an implementation of a timer.
 * It is included in civetweb.c, and does not need to be compiled otherwise.
 */


struct ttimer {
	double time;
	double period;
	int active;
	mg_timer_callback_t func;
	void *arg;
	void *conn;
};


struct ttimers {
	int num_timers;
	int max_timers;
	struct ttimer *timers;
	pthread_mutex_t timer_mutex;
	pthread_cond_t timer_cond;
	pthread_t thread_id;
	int stop;
};


TIMER_API void
timers_exit(struct mg_context *ctx)
{
	if (ctx && ctx->timers) {
		ctx->timers->stop = 1;
		pthread_cond_signal(&ctx->timers->timer_cond);
		pthread_join(ctx->timers->thread_id, NULL);
		pthread_mutex_destroy(&ctx->timers->timer_mutex);
		pthread_cond_destroy(&ctx->timers->timer_cond);
		mg_free(ctx->timers->timers);
		mg_free(ctx->timers);
		ctx->timers = NULL;
	}
}


static void *
timer_thread(void *arg)
{
	struct mg_context *ctx = (struct mg_context *)arg;
	struct ttimers *timers = ctx->timers;
	int i;

	mg_set_thread_name("timer");

	while (!timers->stop) {
		struct timespec now;
		double min_time = -1.0;

		pthread_mutex_lock(&timers->timer_mutex);

		clock_gettime(CLOCK_MONOTONIC, &now);

		for (i = 0; i < timers->num_timers; i++) {
			if (timers->timers[i].active) {
				if (timers->timers[i].time < (now.tv_sec + now.tv_nsec * 1.0E-9)) {
					timers->timers[i].func(timers->timers[i].arg);
					if (timers->timers[i].period > 0.0) {
						timers->timers[i].time += timers->timers[i].period;
					} else {
						timers->timers[i].active = 0;
					}
				}
			}
		}

		for (i = 0; i < timers->num_timers; i++) {
			if (timers->timers[i].active) {
				if ((min_time < 0.0)
				    || (timers->timers[i].time < min_time)) {
					min_time = timers->timers[i].time;
				}
			}
		}

		if (min_time > 0.0) {
			struct timespec ts;
			double dt = min_time - (now.tv_sec + now.tv_nsec * 1.0E-9);
			ts.tv_sec = (time_t)dt;
			ts.tv_nsec = (long)((dt - ts.tv_sec) * 1.0E9);
			pthread_cond_timedwait(&timers->timer_cond,
			                       &timers->timer_mutex,
			                       &ts);
		} else {
			pthread_cond_wait(&timers->timer_cond, &timers->timer_mutex);
		}

		pthread_mutex_unlock(&timers->timer_mutex);
	}

	return NULL;
}


TIMER_API int
timers_init(struct mg_context *ctx)
{
	struct ttimers *timers =
	    (struct ttimers *)mg_calloc(1, sizeof(struct ttimers));
	if (!timers) {
		return -1;
	}

	timers->max_timers = 10;
	timers->timers = (struct ttimer *)
	    mg_calloc(timers->max_timers, sizeof(struct ttimer));
	if (!timers->timers) {
		mg_free(timers);
		return -1;
	}

	pthread_mutex_init(&timers->timer_mutex, NULL);
	pthread_cond_init(&timers->timer_cond, NULL);

	ctx->timers = timers;

	mg_start_thread(timer_thread, ctx);

	return 0;
}


TIMER_API void
timer_add(struct mg_context *ctx,
          double seconds,
          double period,
          int active,
          mg_timer_callback_t func,
          void *arg,
          void *conn)
{
	struct ttimers *timers = ctx->timers;
	struct timespec now;
	int i;

	pthread_mutex_lock(&timers->timer_mutex);

	for (i = 0; i < timers->num_timers; i++) {
		if (!timers->timers[i].active) {
			break;
		}
	}

	clock_gettime(CLOCK_MONOTONIC, &now);

	timers->timers[i].time = now.tv_sec + now.tv_nsec * 1.0E-9 + seconds;
	timers->timers[i].period = period;
	timers->timers[i].active = active;
	timers->timers[i].func = func;
	timers->timers[i].arg = arg;
	timers->timers[i].conn = conn;

	if (i == timers->num_timers) {
		timers->num_timers++;
	}

	pthread_cond_signal(&timers->timer_cond);
	pthread_mutex_unlock(&timers->timer_mutex);
}