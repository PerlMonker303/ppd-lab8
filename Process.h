#pragma once
#include <mpi.h>
#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>

struct Prepare {
	std::string var;
	int ts;
	int sender;
	bool open = false;
};

struct PrepareResponse {
	std::string var;
	int ts;
	int sender;
};

struct SetOperation {
	std::string var;
	int val;
	bool open = false;
};

// stores a set operation on the framework level
struct SetOperationFramework {
	std::string var;
	int val;
	int ts;
};

struct FailedSend {
	SetOperationFramework sof;
	int parent;
};

struct Triplet {
	std::string var;
	int val;
	int ts;
	int dest;
};

class Process
{
private:
	int id;
	int timestamp = 0;
	std::vector<std::string> variables;
	std::unordered_map<std::string, std::vector<int>> processesSubscribed; // for each variable, vector of ids of subscribed processes
	std::vector<int> values; // associated to variables
	std::vector<std::string> log; // contains operations so we know the order they were received in; should be the same for all processes
	std::vector<SetOperation> setOperations;
	int currentSetOperation = 0;
	std::vector<Prepare> receivedPrepares; // <var, ts, sender, index_operation>
	std::vector<PrepareResponse> prepareResponses; // <index of set operation, vector of prepare responses>
	std::vector<SetOperationFramework> frameworkOperations;
	std::vector<FailedSend> failedToSend;

public:
	Process(int id);
	void subscribeToVar(std::string var);
	void displayMemory();
	void addLog(std::string message);
	void displayLog();
	void addSetOperation(std::string var, int val);
	SetOperation runNextSetOperation();
	void incrementTs();
	void addOtherSubscriber(std::string var, int pid);
	void storeReceivedPrepare(std::string var, int ts, int sender);
	void storeReceivedPrepareResponse(std::string var, int ts, int sender);
	bool receivedAllPrepareResponses(std::string var);
	void sendTriplets(int my_rank);
	bool findPrepareForMessage(std::string var, int ts, int sender);

	int getId();
	int getTs();
	void setTs(int ts);
	int getIndexForVariable(std::string var);
	std::vector<int> getSubscribersForVariable(std::string var);
	void setValueForVariable(std::string var, int val);
	void addFrameworkOperation(SetOperationFramework sof);
	void sendNotificationsFromFramework();
	bool receivedAllOperationsForPrepares();
	bool isTimestampSmallerThanOpenMessages(int ts);
	void closePrepare(std::string var);
	void addFailedToSend(SetOperationFramework sof, int parent);
	void retrySendingFailedTriplets();
	int getTSFromReceivedPrepareResponse(std::string var);
	void updateLocalSetOperationTimestamp();
};

