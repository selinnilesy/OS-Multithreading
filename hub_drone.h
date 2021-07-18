//
// Created by Selin Yıldırım on 14.05.2021.
//

#ifndef HUB_H
#define HUB_H
#include "monitor.h"
#include "package.h"

class Drone;

class Hub: public Monitor {
// when following is used as a local variable the method becomes a monitor method. On constructor
// lock is acquired, when function returns, automatically called desctructor unlocks it.
//#define __synchronized__ Lock mutex(this);
public:
    int ID, incomingSize, outgoingSize, chargingSize, senderID, receiverID;
    vector<Package*> incomingPackages;
    vector<Package*> outgoingPackages;
    vector<Drone*> chargingDrones;
    int numSenders; // numSenders are global senders' count.
    sem_t *smutexSenders;
    sem_t *sfullIncoming, *smutexIncoming, *semptyIncoming; // smutexIncoming protects update on incomingPackages vector.
    sem_t *sfullOutgoing, *smutexOutgoing, *semptyOutgoing; // semptyOutgoing signals when 1 package has gone
    sem_t *sfullCharging, *smutexCharging, *semptyCharging;  // sfullCharging signals when drone landed on hub
    bool active;
    int waitingDrone; // for drone call
    sem_t *smutexActive;
    int totalSenders; //size for below array.
    int totalHubCount; // bu hicbi zaman degismicek, totalSenders mutex ile korunuyor ama buna gerek yok cunku degismicek.
    int *distancesToHubs;

    Hub(int ID, int incomingSize, int outgoingSize, int chargingSize, int *distances, int numSenders)
    {
        this->ID = ID;
        this->senderID = this->receiverID = this->waitingDrone = 0;
        this->incomingSize = incomingSize;
        this->outgoingSize = outgoingSize;
        this->chargingSize = chargingSize;
        this->numSenders = this->totalSenders = this->totalHubCount = numSenders;
        this->active=1;
        sfullIncoming = new sem_t;
        if (sem_init(sfullIncoming, 0, 0)) {
            perror("Failed to open semphore for empty");
        }
        smutexIncoming = new sem_t;
        if (sem_init(smutexIncoming, 0, 1)) {
            perror("Failed to open semphore for empty");
        }
        semptyIncoming = new sem_t;
        if (sem_init(semptyIncoming, 0, this->incomingSize)) {
            perror("Failed to open semphore for empty");
        }
        sfullOutgoing = new sem_t;
        if (sem_init(sfullOutgoing, 0, 0)) {
            perror("Failed to open semphore for empty");
        }
        smutexOutgoing = new sem_t;
        if (sem_init(smutexOutgoing, 0, 1)) {
            perror("Failed to open semphore for empty");
        }
        semptyOutgoing = new sem_t;
        if (sem_init(semptyOutgoing, 0, this->outgoingSize)) {
            perror("Failed to open semphore for empty");
        }
        sfullCharging = new sem_t;
        if (sem_init(sfullCharging, 0, 0)) { // will be incremented during input scan.
            perror("Failed to open semphore for empty");
        }
        smutexCharging = new sem_t;
        if (sem_init(smutexCharging, 0, 1)) {
            perror("Failed to open semphore for empty");
        }
        semptyCharging = new sem_t;
        if (sem_init(semptyCharging, 0, this->chargingSize)) {
            perror("Failed to open semphore for empty");
        }
        smutexSenders = new sem_t;
        if (sem_init(smutexSenders, 0, 1)) {
            perror("Failed to open semphore for empty");
        }
        smutexActive = new sem_t;
        if (sem_init(smutexActive, 0, 1)) {
            perror("Failed to open semphore for empty");
        }
        distancesToHubs = distances; // numHubs-1 sized pointer for already allocated distance array in hubs**.
    }
    bool CallDroneFromHubs();
    void AssignAndNotifyDrone(Package*, Drone*);
    int WaitUntilPackageDeposited(); // waits on semptyIncoming
    void*  mainHub(void*);
    void updateCharges();
    void WaitTimeoutOrDrone();
    void HubStopped();
    static void* send_wrapper(void* object)
    {
        reinterpret_cast<Hub*>(object)->mainHub(nullptr);
        return 0;
    }
};

class Drone: public Monitor {

// when following is used as a local variable the method becomes a monitor method. On constructor
// lock is acquired, when function returns, automatically called desctructor unlocks it.
//#define __synchronized__ Lock mutex(this);
public:
    int ID, Speed, StartingHub, MaximumRange, currRange;
    sem_t *sCarryingPackage;    // signalled when assigned.
    sem_t *sCalled;    // signalled when called.
    sem_t *smutexCharging; //to protect currRange.

    sem_t *smutexAssignPackage; // protects assignedPackage & landingTime & currRange.
    Package *assignedPackage;

    long long landingTime;
    sem_t *smutexLandingTime;

    bool charging, called, carryingPackage;

    int callingHubID;
    sem_t *smutexCallingHubID;

    int numHubs; // numSenders are global senders' count.
    sem_t *smutexHubs; // to protect numHubs

    Drone(int ID, int Speed, int StartingHub, int MaximumRange, int numHubs)   // pass "this" to cv constructors
    {
        this->ID = ID;
        this->Speed= Speed;
        this->StartingHub = StartingHub;
        this->charging = 0; // charged initially,
        this->MaximumRange = this->currRange = MaximumRange;
        this ->numHubs = numHubs;
        this ->called = 0;
        this ->carryingPackage = 0;
        this ->smutexCallingHubID = 0;
        this ->assignedPackage = NULL;
        this ->landingTime = timeInMilliseconds();
        smutexLandingTime = new sem_t();
        if (sem_init(smutexLandingTime, 0, 1)) {
            perror("Failed to open drone semphore for empty");
        }
        smutexCallingHubID = new sem_t();
        if (sem_init(smutexCallingHubID, 0, 1)) {
            perror("Failed to open drone semphore for empty");
        }
        sCarryingPackage = new sem_t();
        if (sem_init(sCarryingPackage, 0, 0)) {
            perror("Failed to open drone semphore for empty");
        }
        smutexAssignPackage = new sem_t();
        if (sem_init(smutexAssignPackage, 0, 1)) { // 1 since mutex.
            perror("Failed to open drone semphore for empty");
        }
        sCalled = new sem_t();
        if (sem_init(sCalled, 0, 0)) {
            perror("Failed to open drone semphore for empty");
        }
        smutexCharging = new sem_t();
        if (sem_init(smutexCharging, 0, 0)) {
            perror("Failed to open drone semphore for empty");
        }
        smutexHubs = new sem_t();
        if (sem_init(smutexHubs, 0, 1)) { // 1 since mutex.
            perror("Failed to open drone semphore for empty");
        }
        assignedPackage = NULL;
    }
    void WaitAndReserveDestinationSpace(int);
    int WaitForRange(int);
    int WaitSignalFromHub();
    int TakePackageFromHub(Package*);
    int DropPackageToHub(Package*);
    void WaitAndReserveChargingSpace(int);
    void* mainDrone(void*);
    static void* send_wrapper(void* object)
    {
        reinterpret_cast<Drone*>(object)->mainDrone(nullptr);
        return 0;
    }
};


#endif //HUB_H
