#include "DSM.h"

DSM::DSM(std::vector<int> processes) {
	this->n = processes.size();
	this->processes = processes;
}

void DSM::start(int noProcs) {
	// run all set operations for all processes
	/*
	std::string var;
	int val;
	for (auto p : this->processes) {
		while (true) {
			std::pair<std::string, int> pair = p->runNextSetOperation();
			var = pair.first;
			val = pair.second;

			if (var == "NONE" && val == -1) {
				// no more set operations
				break;
			}

			// search for all processes that subscribed to this value, different from the current process
			std::vector<Process*> subscribers;
			for (auto subscriber : this->processes) {
				if (subscriber->getId() != p->getId() && subscriber->isSubscribedToVar(var)) {
					subscribers.push_back(subscriber);
				}
			}

			// send a prepare message to them
			for (auto subscriber : subscribers) {
				p->incrementTs();
				subscriber->sendPrepareMessage(p, var, val, p->getTs());
			}

			// send a message to yourself as well
			//p->finalSend(p, var, val, p->getTs());
		}
	}
	*/
}