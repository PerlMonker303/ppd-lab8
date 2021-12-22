#include "Process.h"

Process::Process(int id) {
	this->id = id;
}

void Process::subscribeToVar(std::string var) {
	this->variables.push_back(var);
	this->values.push_back(-1);
}

void Process::displayMemory() {
	std::cout << "[Variables for process " << this->id << "]\n";
	for (int i = 0; i < this->variables.size(); i++) {
		std::cout << this->variables[i] << '=' << this->values[i] << '\n';
	}
	std::cout << "[... done]\n";
}

void Process::addLog(std::string message) {
	this->log.push_back(message);
}

void Process::displayLog() {
	std::cout << "[Log for process " << this->id << "]\n";
	for (auto message : this->log) {
		std::cout << message << '\n';
	}
	std::cout << "[... done]\n";
}

void Process::addSetOperation(std::string var, int val) {
	this->setOperations.push_back(std::pair<std::string, int>(var, val));
}

void Process::displayPrepareMessages() {
	std::cout << "[Prepare messages for process " << this->id << "]\n";
	for (auto prepare : this->receivedPrepares) {
		std::cout << std::get<0>(prepare) << ' ' << std::get<1>(prepare) << ' ' << std::get<2>(prepare) << '\n';
	}
	std::cout << "[... done]\n";
}

std::pair<std::string, int> Process::runNextSetOperation() {
	if (this->currentSetOperation < this->setOperations.size()) {
		std::cout << "[" << this->id << "]Running SET(" << this->setOperations[this->currentSetOperation].first << "," << this->setOperations[this->currentSetOperation].second << ")\n";
		this->currentSetOperation++;
		return this->setOperations[this->currentSetOperation-1];
	}
	return std::pair<std::string, int>("NONE", -1);
}

void Process::incrementTs() {
	this->timestamp++;
}

void Process::addOtherSubscriber(std::string var, int pid) {
	this->processesSubscribed[var].push_back(pid);
}

void Process::storeReceivedPrepare(std::string var, int ts, int sender, int index_operation) {
	this->receivedPrepares.push_back(std::tuple<std::string, int, int, int>(var, ts, sender, index_operation));
}

void Process::storeReceivedPrepareResponse(std::string var, int ts, int sender, int index_operation) {
	PrepareResponse pr{ index_operation, ts, sender };
	this->prepareResponses[index_operation].push_back(pr);
}

bool Process::canSendNotifications(std::string var, int index_operation) {
	// the current process is stored already in the prepareResponses, but the subscribers for a variable 
	if (this->prepareResponses[index_operation].size() == this->getSubscribersForVariable(var).size() + 1) {
		return true;
	}
	return false;
}

void Process::sendNotifications(int my_rank, int index_operation) {
	// iterate over responses to prepares, select the ts, sender, variable
	std::vector<Notification> notifications;

	std::string var;
	int val, ts;
	for (auto pr : this->prepareResponses[index_operation]) {
		var = this->setOperations[index_operation].first;
		val = this->setOperations[index_operation].second;
		Notification notif{ var, val, pr.ts, pr.sender};
		notifications.push_back(notif);
	}

	// sort notifications by ts
	Notification aux;
	for (int i = 0; i < notifications.size(); i++) {
		for (int j = i+1; j < notifications.size(); j++) {
			if (notifications[i].ts > notifications[j].ts) {
				aux = notifications[i];
				notifications[i] = notifications[j];
				notifications[j] = aux;
			}
		}
	}
	// send notifications
	int code_send = 10;
	char variable;
	for (auto notif : notifications) {
		variable = notif.var[0];
		val = notif.val;
		ts = notif.ts;
		if (notif.dest == my_rank) {
			// do it locally
			this->setValueForVariable(std::string(1, variable), val);
			// write to the log
			this->incrementTs();
			this->addLog("SET(" + std::string(1, variable) + "," + std::to_string(val) + ") ts=" + std::to_string(this->getTs()));
		}
		else {
			// send it
			MPI_Send(&code_send, 1, MPI_INT, notif.dest, 123, MPI_COMM_WORLD);
			MPI_Send(&variable, 1, MPI_INT, notif.dest, 123, MPI_COMM_WORLD);
			MPI_Send(&val, 1, MPI_INT, notif.dest, 123, MPI_COMM_WORLD);
			MPI_Send(&ts, 1, MPI_INT, notif.dest, 123, MPI_COMM_WORLD);
		}
	}
}

void Process::initializePrepareResponses() {
	for (int i = 0; i < this->setOperations.size(); i++) {
		this->prepareResponses[i] = std::vector<PrepareResponse>();
	}
}

int Process::getId() {
	return this->id;
}

int Process::getTs() {
	return this->timestamp;
}

void Process::setTs(int ts) {
	this->timestamp = ts;
}

int Process::getIndexForVariable(std::string var) {
	for (int i = 0; i < this->variables.size(); i++) {
		if (this->variables[i] == var) {
			return i;
		}
	}
	return -1;
}

std::vector<int> Process::getSubscribersForVariable(std::string var) {
	return this->processesSubscribed[var];
}

void Process::setValueForVariable(std::string var, int val) {
	int idx = this->getIndexForVariable(var);
	if (idx != -1) {
		this->values[idx] = val;
	}
}