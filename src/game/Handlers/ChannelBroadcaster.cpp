#include <thread>
#include <chrono>
#include <mutex>
#include "ChannelBroadcaster.h"
#include "ChannelMgr.h"
#include "World.h"


ChannelBroadcaster::ChannelBroadcaster() : MessageQueue(15)
{
	StartThread();
}

ChannelBroadcaster::~ChannelBroadcaster()
{
	Stop();
}

void ChannelBroadcaster::StartThread()
{
	Worker = new std::thread([this]()
	{
		ThreadProc();
	});
}

void ChannelBroadcaster::Stop()
{
	if (Worker == nullptr)
	{
		return;
	}

	{
		std::lock_guard<std::mutex> Lock(WakeMutex);
		bStopRequested.store(true);
	}
	WakeCondition.notify_one();

	if (Worker->joinable())
	{
		Worker->join();
	}

	delete Worker;
	Worker = nullptr;
}

void ChannelBroadcaster::EnableSendingMessages()
{
	{
		std::lock_guard<std::mutex> Lock(WakeMutex);
		bShouldSentMessages.store(true);
	}
	WakeCondition.notify_one();
	while (!bIsWorking.load() && !sWorld.IsStopped())
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(0));
	}
}

void ChannelBroadcaster::DisableSendingMessages()
{
	{
		std::lock_guard<std::mutex> Lock(WakeMutex);
		bShouldSentMessages.store(false);
	}
	WakeCondition.notify_one();
	while (bIsWorking.load())
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(0));
	}
}

void ChannelBroadcaster::EnqueueMessage(std::string&& Message, const std::string& ChannelName, ObjectGuid PlayerGuid, uint32 Language, Team ChannelTeam, bool bSkipChecks)
{
	MessageQueue.enqueue(ChannelMessage{std::move(Message), ChannelName, PlayerGuid, Language, ChannelTeam, bSkipChecks });
	// No lock here: a wakeup that races the worker's predicate check is lost, but
	// the wait is bounded by WakeTimeout, so the message is only delayed.
	WakeCondition.notify_one();
}

bool ChannelBroadcaster::IsStopping() const
{
	return bStopRequested.load() || sWorld.IsStopped();
}

void ChannelBroadcaster::ThreadProc()
{
	// The timeout only bounds wakeups nobody signals (sWorld.IsStopped(), or an
	// enqueue racing the predicate check); flag changes and enqueues notify at once.
	constexpr std::chrono::milliseconds WakeTimeout(1);

	while (!IsStopping())
	{
		while (bShouldSentMessages.load() && !IsStopping())
		{
			bIsWorking.store(true);

			constexpr int32 MessageLimit = 5;
			int32 MessageIterator = 0;

			ChannelMessage msg;
			while (MessageIterator < MessageLimit && MessageQueue.try_dequeue(msg))
			{
				ChannelMessage& ChanMsg = msg;

				ChannelMgr* ChannelManager = channelMgr(ChanMsg.ChannelTeam);
				Channel* TargetChannel = ChannelManager->GetOrCreateChannel(ChanMsg.ChannelName);
				TargetChannel->Say(ChanMsg.PlayerGuid, ChanMsg.Message.c_str(), ChanMsg.Language, ChanMsg.bSkipChecks);
				MessageIterator++;
			}

			// Queue drained: sleep until a message arrives, sending is disabled or the
			// server stops, instead of spinning a core for the whole map update.
			if (MessageIterator == 0)
			{
				std::unique_lock<std::mutex> Lock(WakeMutex);
				WakeCondition.wait_for(Lock, WakeTimeout, [this]()
				{
					return IsStopping() || !bShouldSentMessages.load() || MessageQueue.size_approx() > 0;
				});
			}
		}
		bIsWorking.store(false);

		// Sending is off between map updates: sleep until EnableSendingMessages() or stop.
		std::unique_lock<std::mutex> Lock(WakeMutex);
		WakeCondition.wait_for(Lock, WakeTimeout, [this]()
		{
			return IsStopping() || bShouldSentMessages.load();
		});
	}
}
