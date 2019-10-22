#include <queue>
#include <iostream>
#include <string>
#include <fstream>
#include <random>
#include <ctime>
#include <deque>
#include <iomanip>
#include <cstdlib>

using namespace std;

#define INVALID_TIME             -1.0
#define INTER_ARRIVAL_TIME_MEAN  1.0
#define SERVICE_TIME_MEAN        4.5
#define INFINITY                 99999999
#define RUN_SIMULATION_TIMES     100

#define result cout

class TimeAvgGenerator;
class AvgGenerator;

TimeAvgGenerator **timeAvg = NULL;
AvgGenerator     *avg      = NULL;

typedef double t_simtime;

class Simulator;

enum EventType {
    EXIT,
    ARRIVAL,
    DEPARTURE
};

class Event {
    private:
        t_simtime eventTime;
        EventType eventType;
        string eventName;

    public:
        Event(t_simtime time, EventType type, string name) {
            this->eventTime = time;
            this->eventType = type;
            this->eventName = name;
        }

        inline Event(t_simtime time)    { this->eventTime = time; }

        inline t_simtime getEventTime() { return this->eventTime; }
        inline EventType getEventType() { return this->eventType; }
        inline string getEventName ()   { return this->eventName; }

        friend ostream & operator << (ostream & out, Event &e);

        virtual void processEvent(Simulator *sim) = 0;
};


ostream & operator << (ostream & out, Event & e) {
    out << e.getEventName() << " at " << e.getEventTime() ;
    return out;
}


/** Comparing two events: event with lower time goes first */
class CompareEvent {
    public:
        bool operator() (Event* &e1, Event* &e2) {
            return e1->getEventTime() > e2->getEventTime();
        }
};


/** stats generators */

class TimeAvgGenerator {

        double accumulatedValue;
        double lastRecordedTime;
        int    lastRecordedValue;
        int    totalSamples;
        double maximum;
        double minimum;

    public:
        TimeAvgGenerator() {
            accumulatedValue = 0.0;
            lastRecordedValue = 0.0;
            lastRecordedTime = 0.0;
            totalSamples = 0;
            maximum = -INFINITY;
            minimum = +INFINITY;
        }

        void pushData(int length, t_simtime timeOfRecord) {
            accumulatedValue += (timeOfRecord - lastRecordedTime) * lastRecordedValue;
            lastRecordedTime = timeOfRecord;
            lastRecordedValue = length;
            totalSamples++;
            if(length < minimum)
                minimum = length;
            if(length > maximum)
                maximum = length;
        }

        double timeAvg() {
            return accumulatedValue / lastRecordedTime;
        }

        int getSampleCount() {
            return totalSamples;
        }

        double getMaxValue() {
            return maximum;
        }

        double getMinValue() {
            return minimum;
        }

};



class AvgGenerator {

        t_simtime accumulatedValue;
        t_simtime valueRecords[1000];
        int noOfSamples;
        double maximum;
        double minimum;

    public:
        AvgGenerator() {
            accumulatedValue = 0;
            noOfSamples = 0;
            maximum = -INFINITY;
            minimum = +INFINITY;
        }

        void pushData(t_simtime value) {
            accumulatedValue += value;
            valueRecords[noOfSamples++] = value;
            if(value < minimum) minimum = value;
            if(value > maximum) maximum = value;
        }

        t_simtime getConfidenceIntervalRange() {
            t_simtime average = this->avg();
            t_simtime val = 0;
            for(int i = 0; i < noOfSamples; i++)
                val += (valueRecords[i] - average)*(valueRecords[i] - average);
            t_simtime estimOfVariance = val / (noOfSamples - 1);
            return sqrt(estimOfVariance * estimOfVariance / noOfSamples);
        }

        t_simtime avg() {
            return accumulatedValue / noOfSamples;
        }

        t_simtime getMaximum() {
            return maximum;
        }

        t_simtime getMinimum() {
            return minimum;
        }
};



/** The queue that we mimic is a queue of customers, hence this class.*/
class Customer {

    public:
        int customerId;                 /** The ids are 1,2,3,...*/
        t_simtime arrivalTime;          /** Following are self explanatory.*/
        t_simtime serviceTimeStartsAt;
        t_simtime departureTime;

        Customer(int customerId, t_simtime arrivalTime) {
            this->customerId = customerId;
            this->arrivalTime = arrivalTime;
            this->serviceTimeStartsAt = INVALID_TIME;
            this->departureTime = INVALID_TIME;
        }

        /** When a customer is destructed, log the arrival and departure times for further calculations.*/
        ~Customer() {
            avg->pushData(abs(this->serviceTimeStartsAt - this->arrivalTime));
        }
};

/** Exponentially distributed random stream. The mean has to be provided in the constructor. here, [ mean = beta = 1 / (lambda) ] */
class RandomStream {
        default_random_engine dre;
        exponential_distribution<t_simtime> distribution;
    public:
        inline RandomStream(t_simtime mean)    { exponential_distribution<t_simtime> temp(1.0/mean); distribution = temp; dre.seed(rand()); }
        inline t_simtime next()                { t_simtime t = distribution(dre); return t; }
};

class Simulator {

    private:
        priority_queue<Event*, vector<Event*>, CompareEvent>  *eventQueue;
        t_simtime simclock;

        deque<Customer*> **Q;            /** the queue of customers */
        bool *serverBusy;                /** used to check if the server is currently busy or not*/
        Customer **customerBeingServed;  /** the customer currently being served.*/
        int customerIdRecord;            /** used to generate the customer ids sequentially 1->2->3->...*/
        t_simtime serviceEndsAt;         /** used to schedule next arrival, next arrival scheduled only when time complies*/

        int N;                           /** number of tellers */

    public:
        RandomStream *serviceTimeStream;                            /** two exponentially distributed random stream to mimic randomness */
        RandomStream *interArrivalTimeStream;

        void scheduleEvent(Event *event);
        void setSimulationEndTime(t_simtime endTime);
        void run();
        t_simtime now();

        void makeServerBusy(int tellerId);                          /** pops one from Q and adds to service. Everyone thus should join Q straightway,
                                                                    and then make a call to this if server is free. This also updates the customer's
                                                                    service start time.*/
        void makeServerIdle(int tellerId);                          /** makes the server idle by setting the necessary flags (server busy,
                                                                    customer being served) and also
                                                                    updates the customer's departure time record. if the server is free, does nothing.*/
        int getFreeTellerId();                                      /** upon arrival, we look for a free teller. If the teller free then the customer
                                                                    joins the service immediately. This method serves the purpose. */
        int getShortestQuedTellerId();                              /** in case no free teller, the customer will join the teller with shortest queue.
                                                                    this method serves this particular purpose */
        int getJockeyableQueueId(int tellerId);                     /** in case a customer leaves a service, in particular, when a queue length at some teller
                                                                    we will have to look around to see if is shortened, we have to look around to find
                                                                    the leftmost Q that has a customer who can jockey. In brief, the customer at the tail
                                                                    of the jockey-able Q will jockey. */
        void jockey(int);                                           /** the event of jockeying is handled here separately as a non-event routine. this
                                                                    routine is invoked from the departure event routine, where the queue from which a
                                                                    departure has occurred is passed as the argument */

        inline void addCustomerToQueue(Customer *C, int tellerId)   { Q[tellerId]->push_back(C); timeAvg[tellerId]->pushData(Q[tellerId]->size(), now()); }
        inline bool isServerBusy(int tellerId)                      { return serverBusy[tellerId]; }
        inline int nextCustomerId()                                 { return ++customerIdRecord; }
        inline t_simtime serviceEndTime()                           { return serviceEndsAt; }

        Simulator(int);
        ~Simulator();

};

class EndEvent : public Event {
    public:
        EndEvent(t_simtime time, EventType type, string name);
        EndEvent(t_simtime time);
        void processEvent(Simulator *sim);
};


class ArrivalEvent : public Event {
    public:
        ArrivalEvent(t_simtime time, EventType type, string name);
        ArrivalEvent(t_simtime time);
        void processEvent(Simulator *sim);
};

class DepartureEvent : public Event {
        int tellerId;             /** this is needed because in the event of a departure, we need to invoke jockey.
                                      To do jockeying, we have to know which teller was serving him, therefore this.*/
    public:
        DepartureEvent(t_simtime time, int tellerId, EventType type, string name);
        DepartureEvent(t_simtime time, int tellerId);
        void processEvent(Simulator *sim);
        inline int getTellerId() { return tellerId; }
};
