#pragma once

#include "SharedDefines.h"
#include "ObjectGuid.h"
#include "Utilities/readerwriterqueue.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>


struct ChannelMessage
{
	std::string Message;
	std::string ChannelName;
	ObjectGuid PlayerGuid;

	uint32 Language;
	Team ChannelTeam;
	bool bSkipChecks;
};

class ChannelBroadcaster
{
public:

	ChannelBroadcaster();
	~ChannelBroadcaster();

	void EnableSendingMessages();
	void DisableSendingMessages();

	void EnqueueMessage(std::string&& Message, const std::string& ChannelName, ObjectGuid PlayerGuid, uint32 Language, Team ChannelTeam, bool bSkipChecks);

protected:

	void StartThread();
	void Stop();

	void ThreadProc();
	bool IsStopping() const;

	moodycamel::ReaderWriterQueue<ChannelMessage> MessageQueue;

	std::thread* Worker = nullptr;
	
	char CacheLineDelimiter[4096];

	std::atomic_bool bShouldSentMessages = false;
	std::atomic_bool bIsWorking = false;
	std::atomic_bool bStopRequested = false;

	// Wakes the worker on enqueue, on enable/disable and on Stop().
	std::mutex WakeMutex;
	std::condition_variable WakeCondition;
};