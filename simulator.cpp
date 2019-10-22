#include "simulator.h"

Simulator::Simulator(int N) {

    this->N = N;
    eventQueue =  new priority_queue <Event*, vector <Event*>, CompareEvent> ();

    Q = new deque<Customer*>*[N];
    for(int i = 0; i < N; i++)
        Q[i] = new deque<Customer*>();

    serverBusy = new bool[N];
    customerBeingServed = new Customer*[N];
    for(int i = 0; i < N; i++) {
        serverBusy[i] = false;
        customerBeingServed[i] = NULL;
    }

    customerIdRecord = 0;

    serviceTimeStream = new RandomStream(SERVICE_TIME_MEAN);
    interArrivalTimeStream = new RandomStream(INTER_ARRIVAL_TIME_MEAN);

}


t_simtime Simulator::now() {
    return this->simclock;
}


int Simulator::getFreeTellerId() {
    for(int i = 0; i < N; i++)
        if(serverBusy[i] == false && Q[i]->empty())
            return i;
    return -1;
}


int Simulator::getShortestQuedTellerId() {
    int tellerId_ = 0;
    int leastCustomerCount = Q[0]->size();
    for(int i = 1; i < N; i++) {
        if(Q[i]->size() < leastCustomerCount) {
            leastCustomerCount = Q[i]->size();
            tellerId_ = i;
        }
    }
    return tellerId_;
}


int Simulator::getJockeyableQueueId(int tellerId) {

    int jumper = -1;
    int minDistance = N * N;
    int totalSizeOfThisTeller = Q[tellerId]->size();
    serverBusy[tellerId] ? totalSizeOfThisTeller++ : totalSizeOfThisTeller += 0;

    for(int otherTellerId = 0; otherTellerId < N; otherTellerId++) {

        if(otherTellerId == tellerId)
            continue;

        int totalSizeOfOtherTeller = Q[otherTellerId]->size();
        serverBusy[otherTellerId] ? totalSizeOfOtherTeller++ : totalSizeOfOtherTeller += 0;
        int distance = abs(tellerId - otherTellerId);

        if(otherTellerId != tellerId && distance < minDistance && totalSizeOfOtherTeller > totalSizeOfThisTeller + 1) {
            jumper = otherTellerId;
            minDistance = distance;
        }

    }

    return jumper;

}


void Simulator::jockey(int tellerId) {

    /** determine if there is customer to jockey. if no, then return immediately */
    int jumperJockeysFrom = getJockeyableQueueId(tellerId);
    if(jumperJockeysFrom == -1) {
        return;
    }

    /** now, there is someone to jockey. remove this customer from the tail, add to this Q */
    Customer *C = Q[jumperJockeysFrom]->back();
    Q[jumperJockeysFrom]->pop_back();
    timeAvg[jumperJockeysFrom]->pushData(Q[jumperJockeysFrom]->size(), now());
    addCustomerToQueue(C, tellerId);

    /** this customer will jockey to "tellerId". determine if the teller with
        "tellerId" is free. if so, then add customer to service by calling
        make server busy. else, put the customer at the end of the queue */
    bool thisTellerBusy = this->serverBusy[tellerId];
    if(!thisTellerBusy) {
        makeServerBusy(tellerId);
        t_simtime departureTime = now() + serviceTimeStream->next();
        DepartureEvent *e = new DepartureEvent(departureTime, tellerId);
        scheduleEvent(e);
    }

}


void Simulator::scheduleEvent(Event *event) {
    this->eventQueue->push(event);
}


void Simulator::run() {

    while (eventQueue->empty() == false) {

        Event *event = this->eventQueue->top();
        this->eventQueue->pop();

        if (event == NULL)
            break;

        // cout << (*event) << endl;

        this->simclock = event->getEventTime();
        event->processEvent(this);
        delete event;

    }

}


void Simulator::setSimulationEndTime(t_simtime endTime) {
    Event *endEvent = new EndEvent(endTime);
    this->scheduleEvent(endEvent);
    this->serviceEndsAt = endTime;
}


Simulator::~Simulator() {
    if (eventQueue) {
        while (!eventQueue->empty()) {
            Event *event = eventQueue->top();
            eventQueue->pop();
            delete event;
        }
        delete eventQueue;
    }
    for(int i = 0; i < N; i++) {
        if(Q[i]) {
            while(!Q[i]->empty()) {
                Customer *C = Q[i]->front();
                Q[i]->pop_front();
                timeAvg[i]->pushData(Q[i]->size(), now());
                delete C;
            }
            delete Q[i];
        }
    }
    delete Q;
    delete serverBusy;
    for(int i = 0; i < N; i++) {
        if(this->customerBeingServed[i]) {
            delete customerBeingServed[i];
        }
    }
    delete customerBeingServed;
    delete serviceTimeStream;
    delete interArrivalTimeStream;

}


void Simulator::makeServerBusy(int tellerId) {
    if(serverBusy[tellerId])
        return;
    if(Q[tellerId]->empty()) {
        return;
    }
    Customer *customer = Q[tellerId]->front();
    Q[tellerId]->pop_front();
    timeAvg[tellerId]->pushData(Q[tellerId]->size(), now());
    customerBeingServed[tellerId] = customer;
    customerBeingServed[tellerId]->serviceTimeStartsAt = now();
    serverBusy[tellerId] = true;
}


void Simulator::makeServerIdle(int tellerId) {
    if(!serverBusy[tellerId] || customerBeingServed[tellerId] == NULL) {
        return;
    }
    serverBusy[tellerId] = false;
    customerBeingServed[tellerId]->departureTime = this->now();
    delete customerBeingServed[tellerId];
    customerBeingServed[tellerId] = NULL;
}

// Event classes

EndEvent::EndEvent(t_simtime time, EventType type, string name): Event (time, type, name) { }
EndEvent::EndEvent(t_simtime time): Event (time, EXIT, string("EXIT")) { }
void EndEvent::processEvent(Simulator *sim) { }


ArrivalEvent::ArrivalEvent(t_simtime time, EventType type, string name): Event (time, type, name) { }
ArrivalEvent::ArrivalEvent(t_simtime time): Event (time, ARRIVAL, string("ARRIVAL")) { }

void ArrivalEvent::processEvent(Simulator *sim) {

    /*** first schedule the next arrival */
    t_simtime nextArrivalTime = sim->now() + sim->interArrivalTimeStream->next();
    if(nextArrivalTime <= sim->serviceEndTime()) {
        ArrivalEvent *event = new ArrivalEvent(nextArrivalTime);
        sim->scheduleEvent(event);
    }

    /** construct a customer object */
    Customer *C = new Customer(sim->nextCustomerId(), sim->now());

    /** is a teller idle? */
    int idleTellerId = sim->getFreeTellerId();

    /** if idle found, then add the customer to this Q, and immediately call makeServerBusy()
        so that this customer is added to the service. After that schedule a departure */
    if(idleTellerId != -1) {

        sim->addCustomerToQueue(C, idleTellerId);
        sim->makeServerBusy(idleTellerId);

        t_simtime departureTime = sim->now() + sim->serviceTimeStream->next();
        DepartureEvent *e = new DepartureEvent(departureTime, idleTellerId);
        sim->scheduleEvent(e);

    }

    /** else, no idle is found. then add this customer C to the queue with shortest length */
    else {
        int shortestQuedTellerID = sim->getShortestQuedTellerId();
        sim->addCustomerToQueue(C, shortestQuedTellerID);
    }

}


DepartureEvent::DepartureEvent(t_simtime time, int tellerId, EventType type, string name) : Event (time, type, name) { this->tellerId = tellerId; }
DepartureEvent::DepartureEvent(t_simtime time, int tellerId) : Event(time, DEPARTURE, string("DEPARTURE")) { this->tellerId = tellerId; }

void DepartureEvent::processEvent(Simulator *sim) {

    //logger << "dept from teller " << tellerId << " at " << sim->now() << endl;

    /*** make the server idle (make the previous customer leave), and then try to make server busy.
         if there is someone at the queue, then the customer from front will be automatically sent
         to the teller for service */

    sim->makeServerIdle(tellerId);
    sim->makeServerBusy(tellerId);

    /*** now determine if the server (teller) is actually busy, then someone from Q has just entered.
         So schedule his departure */

    if(sim->isServerBusy(tellerId)) {
        t_simtime departureTime = sim->now() + sim->serviceTimeStream->next();
        DepartureEvent *event = new DepartureEvent(departureTime, tellerId);
        sim->scheduleEvent(event);
    }

    /*** invoke the jockey routine. this is a non-event routine. jockeying is handled separately in this
         separate method. */

    sim->jockey(tellerId);

}


int main(int argc, char **argv) {

    srand(time(NULL));

    result << "Multi-teller bank with jockeying " << endl;
    result << "________________________________________________________________________________" << endl;
    result << "Total tellers:\t\t\t\t2 to 8" << endl;
    result << "Mean inter-arrival time:\t\t" << INTER_ARRIVAL_TIME_MEAN << " minutes" << endl;
    result << "Mean service time:\t\t\t" << SERVICE_TIME_MEAN << " minutes" << endl;
    result << "Duration:\t\t\t\t8 hours" << endl;

    result << endl << endl;

    result << "#tellers\tAvg q len\tAvg delay\tLeft boundary\tRight boundary" << endl;
    result << "________________________________________________________________________________" << endl;

    for(int numTellers = 4; numTellers <= 9; numTellers++) {

        double accumulatedAverageDelay = 0;
        double accumulatedQueueLength = 0;
        double accumulatedRange = 0;

        for(int i = 0; i < RUN_SIMULATION_TIMES; i++) {

            int N = numTellers;

            Simulator* sim = new Simulator(N);
            timeAvg = new TimeAvgGenerator*[N];
            for(int i = 0; i < N; i++)
                timeAvg[i] = new TimeAvgGenerator();
            avg = new AvgGenerator();

            sim->scheduleEvent(new ArrivalEvent(0.0));
            sim->setSimulationEndTime(480.0);
            sim->run();

            delete sim;

            double timeAverage = 0;
            for(int i = 0; i < N; i++) {
                timeAverage += timeAvg[i]->timeAvg();
            }

            accumulatedAverageDelay += avg->avg();
            accumulatedQueueLength += timeAverage;
            accumulatedRange += avg->getConfidenceIntervalRange();

            delete avg;
            for(int i = 0; i < N; i++)
                delete timeAvg[i];
            delete timeAvg;

        }

        double averageAverageDelay = accumulatedAverageDelay / RUN_SIMULATION_TIMES;
        double averageRange = accumulatedRange / RUN_SIMULATION_TIMES;
        double leftBoundary = averageAverageDelay - averageRange;
        double rightBoundary = averageAverageDelay + averageRange;
        double averageQueueLength = accumulatedQueueLength / RUN_SIMULATION_TIMES;

        printf("%d\t\t%-10.6f\t%-10.6f\t%-10.6f\t%-10.6f\n", numTellers, averageQueueLength, averageAverageDelay, leftBoundary, rightBoundary);
    }

    return 0;

}
