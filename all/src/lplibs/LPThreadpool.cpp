#include "LPThreadpool.h"

// THREADPOOL STRUCTURE

// adds jobs in the threadpool queue without concurrency controls
// SHOULD BE USED CAREFULLY ONLY WHEN WORKERS ARE SLEEPING
void lp_threadpool_addjob_nolock( lp_threadpool* pool, void *(*func)(int, void *), void* args ){

	// ENTER POOL CRITICAL SECTION
	//pthread_mutex_lock( &pool->mutex_pool );
	//////////////////////////////////////

	lp_tpjob *njob = (lp_tpjob*)malloc( sizeof(lp_tpjob) );
	if( !njob ){
		perror( "Could not create a lp_tpjob...\n" );
		return;
	}
	njob->args = args;
	njob->func = func;
	njob->next = 0;

	// empty job queue
	if( pool->pending_jobs == 0 ){
		pool->jobs_head = njob;
		pool->jobs_tail = njob;
	}else{
		pool->jobs_tail->next = njob;
		pool->jobs_tail = njob;
	}

	pool->pending_jobs++;

	//fprintf( stderr, "job added [%d]\n", pool->pending_jobs );

	// EXIT POOL CRITICAL SECTION
	//pthread_mutex_unlock( &pool->mutex_pool );
	//////////////////////////////////////

	// signal any worker_thread that new job is available
	//pthread_cond_signal( &pool->cond_jobs );
}

// signals sleeping workers to wake up
void lp_threadpool_startjobs(lp_threadpool* pool){
	for( int i=0,sz=pool->nthreads; i<sz; i++ ){
		pool->initialSleepThreads[i] = 1;
	}
}


void inline lp_threadpool_addjob( lp_threadpool* pool, void *(*func)(int, void *), void* args ){

	// ENTER POOL CRITICAL SECTION
	pthread_mutex_lock( &pool->mutex_pool );
	//////////////////////////////////////

	lp_tpjob *njob = (lp_tpjob*)malloc( sizeof(lp_tpjob) );
	if( !njob ){
		perror( "Could not create a lp_tpjob...\n" );
		return;
	}
	njob->args = args;
	njob->func = func;
	njob->next = 0;

	// empty job queue
	if( pool->pending_jobs == 0 ){
		pool->jobs_head = njob;
		pool->jobs_tail = njob;
	}else{
		pool->jobs_tail->next = njob;
		pool->jobs_tail = njob;
	}

	pool->pending_jobs++;

	//fprintf( stderr, "job added [%d]\n", pool->pending_jobs );

	// EXIT POOL CRITICAL SECTION
	pthread_mutex_unlock( &pool->mutex_pool );
	//////////////////////////////////////

	// signal any worker_thread that new job is available
	pthread_cond_signal( &pool->cond_jobs );
}

void inline lp_threadpool_fetchjob( lp_threadpool* pool, lp_tpjob *njob ){
	lp_tpjob* job;
	// lock pool
	pthread_mutex_lock( &pool->mutex_pool );

	while( pool->pending_jobs == 0 ){
		pool->synced_threads++;
		//fprintf( stderr, "fecth_job: synced_threads[%d]\n", pool->synced_threads );
		if( pool->synced_threads == pool->nthreads ){
			// signal anyone waiting for complete synchronization
			pthread_cond_broadcast(&pool->sleep);
		}
		pthread_cond_wait( &pool->cond_jobs, &pool->mutex_pool );
		pool->synced_threads--;
	}

	// available job pending
	--pool->pending_jobs;
	job = pool->jobs_head;
	pool->jobs_head = pool->jobs_head->next;
	// if no more jobs available
	if( pool->jobs_head == 0 ){
		pool->jobs_tail = 0;
	}

	//fprintf( stderr, "job removed - remained[%d]\n", pool->pending_jobs );

	njob->args = job->args;
	njob->func = job->func;

	free( job );

	// pool unlock
	pthread_mutex_unlock( &pool->mutex_pool );

	//return job;
}
int inline lp_threadpool_uniquetid( lp_threadpool* pool ){
	// returns an id from 1 to number of threads (eg. threads=12, ids = 1,2,3,4,5,6,7,8,9,10,11,12)
	int _tid;
	pthread_mutex_lock( &pool->mutex_pool );
	_tid = pool->workers_ids;
	pool->workers_ids = ( ( pool->workers_ids + 1 ) % (pool->nthreads+1) )  ;
	pthread_mutex_unlock( &pool->mutex_pool );
	return _tid;
}
void* lp_tpworker_thread( void* _pool ){
	lp_threadpool* pool = ((lp_threadpool*)_pool);
	int _tid=lp_threadpool_uniquetid( pool );

	fprintf( stderr, "thread[%d] entered worker_thread infite\n", _tid );

	// _tid-1 because tids are from 1-NUM_THREADS
	// the value will change when the startJobs function will be called
	while( pool->initialSleepThreads[_tid-1]==0 ){}

    lp_tpjob njob;

	for(;;){
		// fetch next job - blocking method
		lp_threadpool_fetchjob( pool, &njob );

		// execute the function passing in the thread_id - the TID starts from 1 - POOL_THREADS
		njob.func( _tid , njob.args );
	}
	return 0;
}
lp_threadpool* lp_threadpool_init( int threads, int cores ){
	lp_threadpool* pool = (lp_threadpool*)malloc( sizeof(lp_threadpool) );
	pool->workers_ids = 1; // threads start from 1 to NTHREADS
	pool->nthreads = threads;
	pool->pending_jobs = 0;
	pool->jobs_head=0;
	pool->jobs_tail=0;

	pthread_cond_init( &pool->cond_jobs, NULL );
	pthread_cond_init( &pool->sleep, NULL );
	pool->synced_threads = 0;

	pthread_mutex_init( &pool->mutex_pool, NULL );

	// lock pool in order to prevent worker threads to start before initializing the whole pool
	pthread_mutex_lock( &pool->mutex_pool );

	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET( 0 , &mask);
	if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &mask) != 0){
		fprintf(stderr, "Error setting thread affinity\n");
	}


	pthread_t *worker_threads = (pthread_t*)malloc(sizeof(pthread_t)*threads);
	for( int i=0; i<threads; i++ ){
		pthread_create( &worker_threads[i], NULL, reinterpret_cast<void* (*)(void*)>(lp_tpworker_thread), pool );
		fprintf( stderr, "[%ld] thread[%d] added\n", worker_threads[i], i );

		CPU_SET( (i % cores) , &mask);
		if (pthread_setaffinity_np(worker_threads[i], sizeof(cpu_set_t), &mask) != 0){
			fprintf(stderr, "Error setting thread affinity\n");
		}
	}

	pool->initialSleepThreads = (char*)malloc(threads);
	memset(pool->initialSleepThreads, 0, threads);

	pool->worker_threads = worker_threads;

	pthread_barrier_init( &pool->pool_barrier, NULL, 25 );

	pool->headsTime = 0;

	// unlock pool for workers
	pthread_mutex_unlock( &pool->mutex_pool );

	return pool;
}
void inline lp_threadpool_destroy(lp_threadpool* pool){
	pthread_cond_destroy( &pool->sleep );
	pthread_cond_destroy( &pool->cond_jobs );
	free(pool->worker_threads);
	for (lp_tpjob* j = pool->jobs_head, *t = 0; j; j = t) {
		t = j->next;
		free(j);
	}
	free(pool);
}
void synchronize_threads_master(int tid, void * arg){
	lp_threadpool* pool = (lp_threadpool*)arg;
	//fprintf( stderr, "thread[%d] entered synchronization\n", tid );
	pthread_barrier_wait( &pool->pool_barrier );
	//fprintf( stderr, ":: thread[%d] exited synchronization\n", tid );
}
void lp_threadpool_synchronize_master(lp_threadpool* pool){
	for( int i=1; i<=pool->nthreads; i++ ){
		lp_threadpool_addjob( pool, reinterpret_cast<void* (*)(int,void*)>(synchronize_threads_master), (void*)pool);
	}
	synchronize_threads_master(0, (void*)pool);
}
void synchronize_complete(lp_threadpool* pool){
	pthread_mutex_lock( &pool->mutex_pool );
	while( pool->synced_threads < pool->nthreads ){
		pthread_cond_wait( &pool->sleep, &pool->mutex_pool );
		//fprintf( stderr, "sunchronize_complete: synced_threads[%d]\n", pool->synced_threads );
	}
	pthread_mutex_unlock( &pool->mutex_pool );
}