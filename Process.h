#pragma once
#include <mpi.h>
#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>

struct PrepareResponse {
	int corresponding_process_id;
	int ts;
	int sender;
};

struct Notification {
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
	std::vector<std::pair<std::string, int>> setOperations;
	int currentSetOperation = 0;
	std::vector<std::tuple<std::string, int, int, int>> receivedPrepares; // <variable, ts>
	std::unordered_map<int, std::vector<PrepareResponse>> prepareResponses; // <index of set operation, vector of prepare responses>

public:
	Process(int id);
	void subscribeToVar(std::string var);
	void displayMemory();
	void addLog(std::string message);
	void displayLog();
	void addSetOperation(std::string var, int val);
	void displayPrepareMessages();
	std::pair<std::string, int> runNextSetOperation();
	void incrementTs();
	void addOtherSubscriber(std::string var, int pid);
	void storeReceivedPrepare(std::string var, int ts, int sender, int index_operation);
	void storeReceivedPrepareResponse(std::string var, int ts, int sender, int index_operation);
	bool canSendNotifications(std::string var, int index_operation);
	void sendNotifications(int my_rank, int index_operation);
	void initializePrepareResponses();

	int getId();
	int getTs();
	void setTs(int ts);
	int getIndexForVariable(std::string var);
	std::vector<int> getSubscribersForVariable(std::string var);
	void setValueForVariable(std::string var, int val);
};

