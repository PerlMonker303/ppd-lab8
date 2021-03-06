#include <iostream>
#include <mpi.h>
#include "Process.h"

/* Notes:
- USE ONLY SINGLE CHARACTER VARIABLES
- both applications must agree on the order of events
- from the app you make changes, the framework notifies the app back of the changes (log these changes)
- fixed set of variables
- subscriptions only for some variables (receive notifs only for those)
- first, use lamport clocks for deciding which of the set operations occur first
- each time a variable gets modified, updates are sent to all the nodes interested in that variable (all nodes know what the subscriptions are)
// for each variable you know the nodes subscribed to that variable, consider only subscribed nodes to change the variable
- idea: when a variable changes, send message with the change and with lamport clocks to all nodes (subscribed obviously to that var)
        deliver notifications in the order of those timestamps

        when framework receives a message, mark the timestamp, send message to all other subscribers (name of variable, value to be set, ts)
        now every node has these types of messages received. Deliver them to the corresponding app in the order of the ts.
        ISSUE: you don't know when it is safe to deliver a certain message - you can't know if in the future you'll receive a message with a lower ts
- solution: another exchange of messages (introduces a time penalty)
        when set operation comes to framework, the framework sends a prepare message to all subscribers of the set variable
        the subscribers will send back a response to the originator with a TS value

        so:
        when you get a set operation (from app), mark that we have a set and send the prepare messages
        when we get a response to a prepare message mark that and once you have all the responses from prepares, send the notifications
        when you receive a prepare message, keep track of that (and its ts)
        when all prepare responses arrived and all messages for prepares received, then sort set operations and send them in order of ts
        as long as we have prepare operations, stop at the earliest of them and wait until further notifications ?

        compare and exchange - 2 set operations on the same variable, it is kind of random which one succeeds and which one fails


        the purpose of the prepare message is to tell the recepient that it will receive something and to wait for that before
        notifying the app

        - when framework gets set operation, mark that it is open and send prepare messages to subscribers
        - when framework gets a prepare message, mark the operation as open and the timestamp + send back response
        - when framework gets a response to a prepare message, mark it and when it gets all the prepares, send the set messages
        - before sending notifications, compare the timestamps sent as the response to the prepare to messages that are still open
        - you can deliver a notification only if the timestamp is smaller than all timestamps in open messages
        - when you receive a set operation from another framework, close the prepare and use that timestamp you just received
        - store notifs in a queue before delivering them

*/

void worker(int my_rank) {
    // each worker corresponds to a process
    Process* process = new Process(my_rank);
    // receive variables it is subscribed to
    MPI_Status status;
    int nr_variables;
    MPI_Recv(&nr_variables, 1, MPI_INT, 0, 123, MPI_COMM_WORLD, &status);

    int parent = status.MPI_SOURCE;
    std::vector<char> variables;
    variables.resize(nr_variables);
    MPI_Recv(variables.data(), nr_variables, MPI_INT, 0, 123, MPI_COMM_WORLD, &status);

    // subscribe to the received variables
    for (int i = 0; i < nr_variables; i++) {
        process->subscribeToVar(std::string(1, variables[i]));
    }

    // wait for other neighbours subscribed to other variables
    int other_count = 0, other, var;
    MPI_Recv(&other_count, 1, MPI_INT, 0, 123, MPI_COMM_WORLD, &status);
    for (int i = 0; i < other_count; i++) {
        MPI_Recv(&var, 1, MPI_INT, 0, 123, MPI_COMM_WORLD, &status);
        MPI_Recv(&other, 1, MPI_INT, 0, 123, MPI_COMM_WORLD, &status);
        process->addOtherSubscriber(std::string(1, var), other);
    }

    // now receive the operations to be performed
    int nr_operations, val;
    MPI_Recv(&nr_operations, 1, MPI_INT, 0, 123, MPI_COMM_WORLD, &status);
    for (int i = 0; i < nr_operations; i++) {
        MPI_Recv(&var, 1, MPI_INT, 0, 123, MPI_COMM_WORLD, &status);
        MPI_Recv(&val, 1, MPI_INT, 0, 123, MPI_COMM_WORLD, &status);
        process->addSetOperation(std::string(1, var), val);
    }

    // iterate over each operation to be performed
    std::string variable;
    int ts;
    int code_send = 8; // code for the prepare messages
    int code_received = 0; // if -1 then stop
    bool first_time = true;
    SetOperationFramework sof;
    SetOperation so;
    while (code_received != -1) {
        if (!first_time) {
            MPI_Recv(&code_received, 1, MPI_INT, MPI_ANY_SOURCE, 123, MPI_COMM_WORLD, &status);
            parent = status.MPI_SOURCE;
        }
        first_time = false;
        switch (code_received) {
        case(0):
            // select a set operation
            so = process->runNextSetOperation();
            variable = so.var;
            val = so.val;

            if (variable == "NONE" && val == -1) {
                // no more set operations
                code_received = -1;
                break;
            }

            // store the local set operation to the frameworkOperation vector
            // the ts of this should be changed later on when you received all prepare responses
            sof.var = variable;
            sof.val = val;
            sof.ts = process->getTs();
            process->addFrameworkOperation(sof);

            // iterate over each subscriber to the variable of the selected operation
            // and send them a prepare message
            for (auto pid : process->getSubscribersForVariable(variable)) {
                if (pid != process->getId()) { // don't send it to yourself
                    process->incrementTs();
                    ts = process->getTs();
                    code_send = 8;
                    MPI_Send(&code_send, 1, MPI_INT, pid, 123, MPI_COMM_WORLD);
                    MPI_Send(&variable[0], 1, MPI_INT, pid, 123, MPI_COMM_WORLD);
                    MPI_Send(&val, 1, MPI_INT, pid, 123, MPI_COMM_WORLD);
                    MPI_Send(&ts, 1, MPI_INT, pid, 123, MPI_COMM_WORLD);
                }
            }
            code_received = 0;
            break;
        case(-1):
            // stop listening
            break;
        case(8):
            // receiving a prepare message
            MPI_Recv(&var, 1, MPI_INT, parent, 123, MPI_COMM_WORLD, &status);
            MPI_Recv(&val, 1, MPI_INT, parent, 123, MPI_COMM_WORLD, &status);
            MPI_Recv(&ts, 1, MPI_INT, parent, 123, MPI_COMM_WORLD, &status);
            // update timestamp of the current process
            process->setTs(std::max(ts, process->getTs()) + 1);
            ts = process->getTs();
            // store received prepare (marks it as open)
            process->storeReceivedPrepare(std::string(1, var), ts, parent);

            // send a response to the prepare message sender
            code_send = 9; // code for sending back a response to a prepare message
            MPI_Send(&code_send, 1, MPI_INT, parent, 123, MPI_COMM_WORLD);
            // send the variable and the new timestamp
            MPI_Send(&var, 1, MPI_INT, parent, 123, MPI_COMM_WORLD);
            MPI_Send(&val, 1, MPI_INT, parent, 123, MPI_COMM_WORLD);
            MPI_Send(&ts, 1, MPI_INT, parent, 123, MPI_COMM_WORLD);
            break;
        case(9):
            // receiving a response to a prepare message sent from this process
            // receive the variable and the new timestamp
            MPI_Recv(&var, 1, MPI_INT, parent, 123, MPI_COMM_WORLD, &status);
            MPI_Recv(&val, 1, MPI_INT, parent, 123, MPI_COMM_WORLD, &status);
            MPI_Recv(&ts, 1, MPI_INT, parent, 123, MPI_COMM_WORLD, &status);
            // change the ts of the current process
            process->setTs(std::max(ts, process->getTs()) + 1);
            ts = process->getTs();
            // store the fact that you've received a response to a prepare message
            // note that the ts received is stored
            process->storeReceivedPrepareResponse(std::string(1, var), ts, parent);

            // now check whether the triplets can be sent (if all responses were received)
            if (process->receivedAllPrepareResponses(std::string(1, var))) {
                // change the triplet of the local set operation
                process->updateLocalSetOperationTimestamp();
                // send the triplets to the frameworks
                process->sendTriplets(my_rank);
            }
            break;
        case(10):
            // receive the set operation from the other party
            MPI_Recv(&var, 1, MPI_INT, parent, 123, MPI_COMM_WORLD, &status);
            MPI_Recv(&val, 1, MPI_INT, parent, 123, MPI_COMM_WORLD, &status);
            MPI_Recv(&ts, 1, MPI_INT, parent, 123, MPI_COMM_WORLD, &status);
            // increment the ts
            process->setTs(std::max(ts, process->getTs()) + 1);

            // close the prepare
            process->closePrepare(std::string(1, var));

            // store the set operation in the framework
            sof.var = std::string(1, var);
            sof.val = val;
            sof.ts = ts;
            process->addFrameworkOperation(sof);

            // check for failed messages and retry sending them
            process->retrySendingFailedTriplets();

            // now check if you've received all operations for the prepare messages received
            if (process->receivedAllOperationsForPrepares()) {
                process->sendNotificationsFromFramework();
            }

            // stop parent process
            code_received = -1;
            //code_send = -1;
            //MPI_Send(&code_send, 1, MPI_INT, parent, 123, MPI_COMM_WORLD);
            break;
        default:
            std::cout << "Error: invalid code received in process " << my_rank << "; code=" << code_received << '\n';
            code_received = -1;
            break;
        }
    }

    // at the end, display the memory and the log messages
    process->displayMemory();
    process->displayLog();
}

void sendTriplet(char var, int dest, int other) {
    MPI_Send(&var, 1, MPI_INT, dest, 123, MPI_COMM_WORLD);
    MPI_Send(&other, 1, MPI_INT, dest, 123, MPI_COMM_WORLD);
}

void sendOperation(char var, int val, int dest) {
    MPI_Send(&var, 1, MPI_INT, dest, 123, MPI_COMM_WORLD);
    MPI_Send(&val, 1, MPI_INT, dest, 123, MPI_COMM_WORLD);
}

void example1(int noProcs) {
    // example 1 (the one from the lecture page)
    std::vector<int> processes;
    // send variables X, Y to p1
    std::vector<char> variables_1{ 'X', 'Y' };
    int nr_variables_1 = variables_1.size();
    MPI_Send(&nr_variables_1, 1, MPI_INT, 1, 123, MPI_COMM_WORLD);
    MPI_Ssend(variables_1.data(), nr_variables_1, MPI_INT, 1, 123, MPI_COMM_WORLD);

    // send variables X, Y to p2
    std::vector<char> variables_2{ 'X', 'Y' };
    int nr_variables_2 = variables_2.size();
    MPI_Send(&nr_variables_2, 1, MPI_INT, 2, 123, MPI_COMM_WORLD);
    MPI_Ssend(variables_2.data(), nr_variables_2, MPI_INT, 2, 123, MPI_COMM_WORLD);

    processes.push_back(1);
    processes.push_back(2);

    // send to each process, for each variable, all other process ids that subscribed to that variable
    // first send how many triples will be sent
    int for_p1 = 2, for_p2 = 2;
    MPI_Send(&for_p1, 1, MPI_INT, 1, 123, MPI_COMM_WORLD);
    MPI_Send(&for_p2, 1, MPI_INT, 2, 123, MPI_COMM_WORLD);
    // for X, send p2 to p1
    sendTriplet('X', 1, 2);
    // for Y, send p2 to p1
    sendTriplet('Y', 1, 2);
    // for X, send p1 to p2
    sendTriplet('X', 2, 1);
    // for Y, send p1 to p2
    sendTriplet('Y', 2, 1);

    // send to each process the operations to be performed
    // send Set(X, 5) to p1
    int nr_operations_1 = 1;
    MPI_Send(&nr_operations_1, 1, MPI_INT, 1, 123, MPI_COMM_WORLD);
    sendOperation('X', 5, 1);
    // send Set(Y, 7) to p1
    int nr_operations_2 = 1;
    MPI_Send(&nr_operations_2, 1, MPI_INT, 2, 123, MPI_COMM_WORLD);
    sendOperation('Y', 7, 2);
}

void example2(int noProcs) {
    // example 2 (the one from the lecture class)
    std::vector<int> processes;
    // send variables A, B, E to p1 and p2
    std::vector<char> variables_1{ 'A', 'B', 'E' };
    std::vector<char> variables_2{ 'A', 'B', 'E' };
    int nr_variables_1 = variables_1.size();
    int nr_variables_2 = variables_2.size();
    MPI_Send(&nr_variables_1, 1, MPI_INT, 1, 123, MPI_COMM_WORLD);
    MPI_Ssend(variables_1.data(), nr_variables_1, MPI_INT, 1, 123, MPI_COMM_WORLD);
    MPI_Send(&nr_variables_2, 1, MPI_INT, 2, 123, MPI_COMM_WORLD);
    MPI_Ssend(variables_2.data(), nr_variables_2, MPI_INT, 2, 123, MPI_COMM_WORLD);

    // send variables C, D, E to p3 and p4
    std::vector<char> variables_3{ 'C', 'D', 'E' };
    std::vector<char> variables_4{ 'C', 'D', 'E' };
    int nr_variables_3 = variables_3.size();
    int nr_variables_4 = variables_4.size();
    MPI_Send(&nr_variables_3, 1, MPI_INT, 3, 123, MPI_COMM_WORLD);
    MPI_Ssend(variables_3.data(), nr_variables_3, MPI_INT, 3, 123, MPI_COMM_WORLD);
    MPI_Send(&nr_variables_4, 1, MPI_INT, 4, 123, MPI_COMM_WORLD);
    MPI_Ssend(variables_4.data(), nr_variables_4, MPI_INT, 4, 123, MPI_COMM_WORLD);

    processes.push_back(1);
    processes.push_back(2);
    processes.push_back(3);
    processes.push_back(4);

    // send to each process, for each variable, all other process ids that subscribed to that variable
    // first send how many triples will be sent
    int for_p1 = 4, for_p2 = 4, for_p3 = 3, for_p4 = 3;
    MPI_Send(&for_p1, 1, MPI_INT, 1, 123, MPI_COMM_WORLD);
    MPI_Send(&for_p2, 1, MPI_INT, 2, 123, MPI_COMM_WORLD);
    MPI_Send(&for_p3, 1, MPI_INT, 3, 123, MPI_COMM_WORLD);
    MPI_Send(&for_p4, 1, MPI_INT, 4, 123, MPI_COMM_WORLD);
    // for A, send p2 to p1
    sendTriplet('A', 1, 2);
    // for B, send p2 to p1
    sendTriplet('B', 1, 2);
    // for E, send p2 to p1
    sendTriplet('E', 1, 2);
    // for A, send p1 to p2
    sendTriplet('A', 2, 1);
    // for B, send p1 to p2
    sendTriplet('B', 2, 1);
    // for E, send p2 to p1
    sendTriplet('E', 2, 1);
    // for C, send p4 to p3
    sendTriplet('C', 3, 4);
    // for D, send p4 to p3
    sendTriplet('D', 3, 4);
    // for E, send p4 to p3
    sendTriplet('E', 3, 4);
    // for C, send p3 to p4
    sendTriplet('C', 4, 3);
    // for D, send p3 to p4
    sendTriplet('D', 4, 3);
    // for E, send p3 to p4
    sendTriplet('E', 4, 3);

    // send to each process the operations to be performed
    // send 4 operations to p1
    int nr_operations_1 = 4;
    MPI_Send(&nr_operations_1, 1, MPI_INT, 1, 123, MPI_COMM_WORLD);
    sendOperation('A', 5, 1);
    sendOperation('B', 4, 1);
    sendOperation('A', 6, 1);
    sendOperation('E', 7, 1);
    // send 4 operations to p2
    int nr_operations_2 = 4;
    MPI_Send(&nr_operations_2, 1, MPI_INT, 2, 123, MPI_COMM_WORLD);
    sendOperation('A', 5, 2);
    sendOperation('B', 4, 2);
    sendOperation('A', 6, 2);
    sendOperation('E', 7, 2);
    sendOperation('Y', 7, 2);
    // send 3 operations to p3
    int nr_operations_3 = 3;
    MPI_Send(&nr_operations_3, 1, MPI_INT, 3, 123, MPI_COMM_WORLD);
    sendOperation('C', 4, 3);
    sendOperation('C', 5, 3);
    sendOperation('E', 7, 3);
    // send 3 operations to p4
    int nr_operations_4 = 3;
    MPI_Send(&nr_operations_4, 1, MPI_INT, 4, 123, MPI_COMM_WORLD);
    sendOperation('C', 4, 4);
    sendOperation('C', 5, 4);
    sendOperation('E', 7, 4);
}

// run using:
// - mpiexec -n 3 lab8
// - mpiexec -n 5 lab8
int main()
{
    MPI_Init(NULL, NULL);

    int my_rank, noProcs;
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &noProcs);

    if (my_rank == 0) {
        // parent
        if (noProcs == 3) {
            example1(noProcs - 1);
        }
        else if (noProcs == 5) {
            example2(noProcs - 1);
        }
    }
    else {
        // worker
        worker(my_rank);
    }
    
    MPI_Finalize();

    return 0;
}
