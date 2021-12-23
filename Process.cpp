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
	SetOperation so{ var, val };
	this->setOperations.push_back(so);
}

SetOperation Process::runNextSetOperation() {
	if (this->currentSetOperation < this->setOperations.size()) {
		std::cout << "[" << this->id << "]Running SET(" << this->setOperations[this->currentSetOperation].var << "," << this->setOperations[this->currentSetOperation].val << ")\n";
		this->currentSetOperation++;
		return this->setOperations[this->currentSetOperation-1];
	}
	return SetOperation{ "NONE", -1 };
}

void Process::incrementTs() {
	this->timestamp++;
}

void Process::addOtherSubscriber(std::string var, int pid) {
	this->processesSubscribed[var].push_back(pid);
}

void Process::storeReceivedPrepare(std::string var, int ts, int sender) {
	Prepare p{ var, ts, sender, true };
	this->receivedPrepares.push_back(p);
}

void Process::storeReceivedPrepareResponse(std::string var, int ts, int sender) {
	PrepareResponse pr{ var, ts, sender };
	this->prepareResponses.push_back(pr);
}

bool Process::receivedAllPrepareResponses(std::string var) {
	// all prepare messages should've been received
	return this->prepareResponses.size() == this->getSubscribersForVariable(var).size();
}

void Process::sendTriplets(int my_rank) {
	// go over the prepare responses and send the triplets
	std::vector<Triplet> triplets;

	std::string var;
	int val, ts;
	for (auto pr : this->prepareResponses) {
		Triplet triplet{ pr.var, this->setOperations[0].val, pr.ts, pr.sender };
		triplets.push_back(triplet);
	}

	// sort triplets by ts
	Triplet aux;
	for (int i = 0; i < triplets.size(); i++) {
		for (int j = i+1; j < triplets.size(); j++) {
			if (triplets[i].ts > triplets[j].ts) {
				aux = triplets[i];
				triplets[i] = triplets[j];
				triplets[j] = aux;
			}
		}
	}

	// send triplets if their ts is smaller than the open messages timestamps
	int code_send = 10;
	char variable;
	SetOperationFramework sof;
	for (auto triplet : triplets) {
		// for each triplet
		variable = triplet.var[0];
		val = triplet.val;
		ts = triplet.ts;
		// condition here blocks - DEADLOCK
		/*Explanation: before sending a triplet to another party,
		* the operation to be sent must have a lower timestamp than
		* any of the timestamps of the open prepare messages received.
		* The issue is that there is always an open prepare message
		* with a lower timestamp. In order to close that message we must receive
		* a triplet from the third party (the one we are expecting), however
		* that triplet never arrives since the other party has the same issue: it can't
		* send the triplet because there is an open prepare message with a lower timestamp.
		* To fix that one we must send the triplet from the current framework, but we are unable to
		* do that. So this is an ugly deadlock.
		* Solution: - force the first framework to send its value anyway (to break the deadlock)
		*/
		if(this->isTimestampSmallerThanOpenMessages(ts) || my_rank == 1){
		//if (this->isTimestampSmallerThanOpenMessages(ts)) { // this would be ideal
			// increment the ts
			this->incrementTs();
			ts = this->timestamp;
			if (triplet.dest == my_rank) {
				SetOperationFramework sof;
				sof.var = variable;
				sof.val = val;
				sof.ts = ts;
				this->addFrameworkOperation(sof);
			}
			else {
				// send it
				MPI_Send(&code_send, 1, MPI_INT, triplet.dest, 123, MPI_COMM_WORLD);
				MPI_Send(&variable, 1, MPI_INT, triplet.dest, 123, MPI_COMM_WORLD);
				MPI_Send(&val, 1, MPI_INT, triplet.dest, 123, MPI_COMM_WORLD);
				MPI_Send(&ts, 1, MPI_INT, triplet.dest, 123, MPI_COMM_WORLD);
			}
		}
		else {
			std::cout << "[" << my_rank << "]failed\n";
			sof.var = variable;
			sof.val = val;
			sof.ts = ts;
			this->addFailedToSend(sof, triplet.dest);
		}
	}
}

bool Process::findPrepareForMessage(std::string var, int ts, int sender) {
	for (auto pm : this->receivedPrepares) {
		if (pm.var == var && pm.sender == sender) {
			return true;
		}
	}
	return false;
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

void Process::addFrameworkOperation(SetOperationFramework sof) {
	// add or update
	int i = 0;
	for (auto fo : this->frameworkOperations) {
		if (fo.var == sof.var) {
			this->frameworkOperations[i] = sof;
			return;
		}
		i++;
	}
	this->frameworkOperations.push_back(sof);
}

void Process::sendNotificationsFromFramework() {
	// go through prepare messages and take a look at those timestamps
	SetOperationFramework sof;
	for (int i = 0; i < this->frameworkOperations.size(); i++) {
		sof = this->frameworkOperations[i];
		for (auto pr : this->receivedPrepares) {
			if (pr.var == sof.var) {
				// compare the timestamps
				if (pr.ts > sof.ts) {
					this->frameworkOperations[i].ts = pr.ts;
				}
			}
		}
	}
	// sort them
	SetOperationFramework aux;
	for (int i = 0; i < this->frameworkOperations.size(); i++) {
		for (int j = i+1; j < this->frameworkOperations.size(); j++) {
			if (this->frameworkOperations[i].ts > this->frameworkOperations[j].ts) {
				aux = this->frameworkOperations[i];
				this->frameworkOperations[i] = this->frameworkOperations[j];
				this->frameworkOperations[j] = aux;
			}
		}
	}

	// "send" notifications
	for (auto sof : this->frameworkOperations) {
		// set the value
		this->setValueForVariable(sof.var, sof.val);
		this->addLog("NOTIFY(" + sof.var + "," + std::to_string(sof.val) + ") ts=" + std::to_string(sof.ts));
	}
}

bool Process::receivedAllOperationsForPrepares() {
	return this->receivedPrepares.size() == this->frameworkOperations.size() - 1;
}

bool Process::isTimestampSmallerThanOpenMessages(int ts) {
	for (auto pr : this->receivedPrepares) {
		if (pr.open) {
			if (ts > pr.ts) {
				return false;
			}
		}
	}
	return true;
}

void Process::closePrepare(std::string var) {
	for (int i = 0; i < this->receivedPrepares.size(); i++) {
		if (this->receivedPrepares[i].var == var) {
			this->receivedPrepares[i].open = false;
		}
	}
}

void Process::addFailedToSend(SetOperationFramework sof, int parent) {
	FailedSend fs{sof, parent};
	this->failedToSend.push_back(fs);
}

void Process::retrySendingFailedTriplets() {
	std::vector<FailedSend> stillFailed;
	for (auto fs : this->failedToSend) {
		if (this->isTimestampSmallerThanOpenMessages(fs.sof.ts)) {
			this->incrementTs();
			// take ts from the prepare message response
			int ts = this->getTSFromReceivedPrepareResponse(fs.sof.var);
			std::cout << "Retry success with ts=" << ts << ",var=" << fs.sof.var << "\n";
			int code_send = 10;
			char var = fs.sof.var[0];
			int val = fs.sof.val;
			MPI_Send(&code_send, 1, MPI_INT, fs.parent, 123, MPI_COMM_WORLD);
			MPI_Send(&var, 1, MPI_INT, fs.parent, 123, MPI_COMM_WORLD);
			MPI_Send(&val, 1, MPI_INT, fs.parent, 123, MPI_COMM_WORLD);
			MPI_Send(&ts, 1, MPI_INT, fs.parent, 123, MPI_COMM_WORLD);
		}
		else {
			stillFailed.push_back(fs);
		}
	}
	this->failedToSend = stillFailed;
}

int Process::getTSFromReceivedPrepareResponse(std::string var) {
	for (auto pr : this->prepareResponses) {
		if (var == pr.var) {
			if (var == "X") { // the first one to break the deadlock
				return pr.ts + 1;
			}
			return pr.ts;
		}
	}
	return -1;
}

void Process::updateLocalSetOperationTimestamp() {
	// current operation is always the first one since it was added at the beginning
	this->frameworkOperations[0].ts = getTSFromReceivedPrepareResponse(this->frameworkOperations[0].var);
}