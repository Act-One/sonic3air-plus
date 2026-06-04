/*
*	rmx Library
*	Copyright (C) 2008-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*
*	Thread
*		Helper classes for multi-threading (using SDL).
*/

#pragma once

#include <atomic>


namespace rmx
{

	class ThreadBase;


	// Simple mutex wrapper
	class Mutex
	{
	public:
		inline Mutex()		 { mMutex = SDL_CreateMutex(); }
		inline ~Mutex()		 { SDL_DestroyMutex(mMutex); }

		inline void lock()	 { SDL_LockMutex(mMutex); }
		inline void unlock() { SDL_UnlockMutex(mMutex); }

	private:
		SDL_mutex* mMutex = nullptr;
	};


	// Thread manager
	class ThreadManager
	{
	public:
		void registerThread(ThreadBase& thread);
		void unregisterThread(ThreadBase& thread);

	private:
		std::set<ThreadBase*> mThreads;
	};


	// Base class for threads
	class ThreadBase
	{
	public:
		ThreadBase();
		ThreadBase(const std::string& name);
		~ThreadBase();

		void startThread();
		void signalStopThread(bool join = false);

		void joinThread();

		bool isThreadRunning() const  { return mIsThreadRunning.load(std::memory_order_acquire); }
#if defined(PLATFORM_WIIU)
		void setWiiUThreadAffinity(uint32 affinity);
#endif

	protected:
		// This is the method to override
		virtual void threadFunc() = 0;

	private:
		static int runThreadStatic(void* data);
		void runThreadInternal();

	protected:
		std::atomic_bool mShouldBeRunning = false;		// If set to false, the thread should stop itself; this has to be implemented in the sub-class

	private:
		SDL_Thread* mSDLThread = nullptr;
		std::string mName;
		std::atomic_bool mIsThreadRunning = false;		// Set as long as the actual thread is running
#if defined(PLATFORM_WIIU)
		uint32 mWiiUThreadAffinity = 0;
#endif
		SinglePtr<ThreadManager> mManager;
	};

}
