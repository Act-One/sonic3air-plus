/*
*	rmx Library
*	Copyright (C) 2008-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "rmxmedia.h"
#if defined(PLATFORM_WIIU)
	#include <coreinit/thread.h>
#endif


namespace rmx
{

	JobManager::JobManager()
	{
		mConditionVariable = SDL_CreateCond();
		mConditionLock = SDL_CreateMutex();
	}

	JobManager::~JobManager()
	{
		shutdown();
		if (nullptr != mConditionVariable)
		{
			SDL_DestroyCond(mConditionVariable);
			mConditionVariable = nullptr;
		}
		if (nullptr != mConditionLock)
		{
			SDL_DestroyMutex(mConditionLock);
			mConditionLock = nullptr;
		}
	}

	void JobManager::shutdown()
	{
		if (nullptr == mConditionLock)
			return;

		SDL_LockMutex(mConditionLock);
		mSearchforJobs = false;
		for (JobBase* job : mJobs)
		{
			if (nullptr != job)
				job->mJobShouldBeRunning.store(false, std::memory_order_release);
		}
		SDL_CondBroadcast(mConditionVariable);
		SDL_UnlockMutex(mConditionLock);

		stopAllThreads();

		SDL_LockMutex(mConditionLock);
		for (JobBase* job : mJobs)
		{
			if (nullptr == job)
				continue;
			job->mRegisteredAtManager = nullptr;
			job->mJobPriority = -1.0f;
			job->mJobShouldBeRunning.store(false, std::memory_order_release);
			job->mJobState.store(JobBase::JobState::INACTIVE, std::memory_order_release);
		}
		mJobs.clear();
		mNextDelayedJobTicks = 0xffffffff;
		SDL_UnlockMutex(mConditionLock);
	}

	void JobManager::setMaxThreads(int count)
	{
		mMaxThreads = clamp(count, 0, 16);
	}

	void JobManager::insertJob(JobBase& job)
	{
		if (nullptr != job.mRegisteredAtManager)
		{
			if (job.mRegisteredAtManager != this)
				return;
			const JobBase::JobState state = job.mJobState.load(std::memory_order_acquire);
			if (state != JobBase::JobState::INACTIVE && state != JobBase::JobState::DONE)
				return;

			// Job is already registered here, but needs to have its state reset back to waiting
		}
		else
		{
			// Register job here
			job.mRegisteredAtManager = this;

			// Make sure we have enough worker threads
			//  -> TODO: Only create a new one if all existing threads are actually busy
			const int index = (int)mThreads.size();
			if (index < mMaxThreads)
			{
				JobWorkerThread* thread = new JobWorkerThread(*this, index);
				mThreads.push_back(thread);
				thread->startThread();
			}
		}

		// Job is ready to be processed
		job.mJobState.store(JobBase::JobState::WAITING, std::memory_order_release);

		// Wake up a thread
		if (!mThreads.empty())
		{
			SDL_LockMutex(mConditionLock);
			mJobs.push_back(&job);
			SDL_CondSignal(mConditionVariable);
			SDL_UnlockMutex(mConditionLock);
		}
		else
		{
			// In case there are no worker threads, execute on the calling thread
			job.executeOnCallingThread();
		}
	}

	void JobManager::insertJob(JobBase& job, float priority)
	{
		job.mJobPriority = priority;
		insertJob(job);
	}

	void JobManager::removeJob(JobBase& job)
	{
		if (job.mRegisteredAtManager != this)
			return;

		bool wasRemoved = false;
		bool wasRunning = false;
		SDL_LockMutex(mConditionLock);
		for (size_t i = 0; i < mJobs.size(); ++i)
		{
			if (mJobs[i] == &job)
			{
				// Swap with last
				if (i + 1 < mJobs.size())
				{
					mJobs[i] = mJobs.back();
				}
				mJobs.pop_back();
				wasRemoved = true;
			}
		}
		wasRunning = (job.mJobState.load(std::memory_order_acquire) == JobBase::JobState::RUNNING);
		job.mJobPriority = -1.0f;
		job.mJobShouldBeRunning.store(false, std::memory_order_release);
		job.mRegisteredAtManager = nullptr;
		SDL_UnlockMutex(mConditionLock);

		if (wasRemoved || wasRunning)
		{
			// Wait until job execution is done
			while (job.mJobState.load(std::memory_order_acquire) == JobBase::JobState::RUNNING)
			{
				SDL_Delay(1);
			}
			job.mJobState.store(JobBase::JobState::INACTIVE, std::memory_order_release);
		}
	}

	int JobManager::getFinishedCount()
	{
		int count = 0;
		SDL_LockMutex(mConditionLock);
		for (JobBase* job : mJobs)
		{
			if (job->isJobDone())
				++count;
		}
		SDL_UnlockMutex(mConditionLock);
		return count;
	}

	JobBase* JobManager::getNextJob()
	{
		SDL_LockMutex(mConditionLock);
		JobBase* job = getNextJobInternal();
		SDL_UnlockMutex(mConditionLock);
		return job;
	}

	JobBase* JobManager::getNextJobBlocking()
	{
		// Wait until there's at leat one job available
		SDL_LockMutex(mConditionLock);
		JobBase* job = getNextJobInternal();
		while (nullptr == job && mSearchforJobs)
		{
			// Using a time-out for two reasons:
			//  - to have a chance to check if "mShouldBeRunning" changed outside
			//  - to react to a delayed job, if there's no other jobs at the moment
			uint32 timeoutMilliseconds = 100;
			if (mNextDelayedJobTicks != 0xffffffff)
			{
				const uint32 currentTicks = SDL_GetTicks();
				if (mNextDelayedJobTicks > currentTicks)
					timeoutMilliseconds = mNextDelayedJobTicks - currentTicks;
			}
			SDL_CondWaitTimeout(mConditionVariable, mConditionLock, timeoutMilliseconds);
			job = getNextJobInternal();
		}
		SDL_UnlockMutex(mConditionLock);
		return job;
	}

	void JobManager::getJobList(std::vector<JobBase*>& output)
	{
		SDL_LockMutex(mConditionLock);
		output = mJobs;
		SDL_UnlockMutex(mConditionLock);
	}

	void JobManager::onJobChanged()
	{
		SDL_LockMutex(mConditionLock);
		SDL_CondSignal(mConditionVariable);
		SDL_UnlockMutex(mConditionLock);
	}

	JobBase* JobManager::getNextJobInternal()
	{
		// Select waiting job with highest priority
		mNextDelayedJobTicks = 0xffffffff;	// This will get updated as well
		JobBase* bestJob = nullptr;
		const uint32 currentTicks = SDL_GetTicks();
		for (JobBase* job : mJobs)
		{
			if (job->mJobState.load(std::memory_order_acquire) == JobBase::JobState::WAITING)
			{
				if (job->mJobDelayUntilTicks <= currentTicks)
				{
					if (nullptr == bestJob || job->mJobPriority > bestJob->mJobPriority)
					{
						bestJob = job;
					}
				}
				else
				{
					if (job->mJobPriority >= 0.0f && job->mJobDelayUntilTicks < mNextDelayedJobTicks)
					{
						mNextDelayedJobTicks = job->mJobDelayUntilTicks;
					}
				}
			}
		}
		if (nullptr != bestJob)
		{
			// Ignore priorities below 0.0f
			if (bestJob->mJobPriority >= 0.0f)
			{
				bestJob->mJobShouldBeRunning.store(true, std::memory_order_release);
				bestJob->mJobState.store(JobBase::JobState::RUNNING, std::memory_order_release);
			}
			else
			{
				bestJob = nullptr;
			}
		}
		return bestJob;
	}

	void JobManager::stopAllThreads()
	{
		mSearchforJobs = false;
		for (JobWorkerThread* thread : mThreads)
		{
			thread->signalStopThread(false);
		}
		for (JobWorkerThread* thread : mThreads)
		{
			thread->joinThread();
		}
		for (JobWorkerThread* thread : mThreads)
		{
			delete thread;
		}
		mThreads.clear();
	}



	void JobBase::setJobPriority(float priority)
	{
		// Jobs with negative priority are deactivated, i.e. won't get processed
		//  -> But when this changes, a thread possibly needs to be woken up
		const bool wakeUpThread = (mJobPriority < 0.0f && priority >= 0.0f);
		mJobPriority = priority;

		if (wakeUpThread && nullptr != mRegisteredAtManager)
		{
			mRegisteredAtManager->onJobChanged();
		}
	}

	void JobBase::setJobDelayUntilTicks(uint32 sdlTicks)
	{
		// If the job delay gets reduced (possibly to zero, i.e. deactivating the delay), a thread possibly needs to be woken up
		const bool wakeUpThread = (sdlTicks < mJobDelayUntilTicks);
		mJobDelayUntilTicks = sdlTicks;

		if (wakeUpThread && nullptr != mRegisteredAtManager)
		{
			mRegisteredAtManager->onJobChanged();
		}
	}

	bool JobBase::callJobFuncOnCallingThread()
	{
		mJobShouldBeRunning.store(true, std::memory_order_release);
		mJobState.store(JobBase::JobState::RUNNING, std::memory_order_release);

		// Call job function implementation once
		const bool result = jobFunc();
		if (result)
		{
			// Job is done
			mJobState.store(JobBase::JobState::DONE, std::memory_order_release);
		}
		else
		{
			// Set back to waiting state
			mJobState.store(JobBase::JobState::WAITING, std::memory_order_release);
		}
		return result;
	}

	void JobBase::executeOnCallingThread()
	{
		if (mJobState.load(std::memory_order_acquire) <= JobBase::JobState::WAITING)
		{
			mJobShouldBeRunning.store(true, std::memory_order_release);
			mJobState.store(JobBase::JobState::RUNNING, std::memory_order_release);

			// Execute until done
			while (!jobFunc())
			{
			}

			mJobShouldBeRunning.store(false, std::memory_order_release);
			mJobState.store(JobBase::JobState::DONE, std::memory_order_release);
		}
	}



	JobWorkerThread::JobWorkerThread(JobManager& jobManager, int index) :
		ThreadBase("rmx Job Worker " + std::to_string(index)),
		mJobManager(jobManager)
	{
#if defined(PLATFORM_WIIU)
		static const uint32 WIIU_JOB_WORKER_AFFINITIES[] =
		{
			OS_THREAD_ATTRIB_AFFINITY_CPU0,
			OS_THREAD_ATTRIB_AFFINITY_CPU1
		};
		setWiiUThreadAffinity(WIIU_JOB_WORKER_AFFINITIES[index % 2]);
#else
		(void)index;
#endif
	}

	void JobWorkerThread::threadFunc()
	{
		while (mShouldBeRunning)
		{
			JobBase* job = mJobManager.getNextJobBlocking();
			if (nullptr != job)
			{
				// Execute job
				const bool result = job->jobFunc();
				if (result)
				{
					// Job is done
					job->mJobState.store(JobBase::JobState::DONE, std::memory_order_release);
					mJobManager.removeJob(*job);
				}
				else
				{
					// Set back to waiting state
					//  -> Note that the job's priority might have changed, or there's another job with higher priority now, so don't just continue with this job
					if (job->mRegisteredAtManager == &mJobManager && job->mJobShouldBeRunning.load(std::memory_order_acquire))
					{
						job->mJobState.store(JobBase::JobState::WAITING, std::memory_order_release);
					}
					else
					{
						job->mJobState.store(JobBase::JobState::DONE, std::memory_order_release);
					}
				}
			}
		}
	}

}
