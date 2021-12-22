#pragma once
#include <iostream>
#include <string>
#include <vector>
#include "Process.h"

class DSM
{
private:
	int n; // nr of processes
	std::vector<int> processes; // store only ids
	std::vector<std::string> variables;
public:
	DSM(std::vector<int> processes);
	void start(int noProcs);
};