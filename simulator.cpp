#include <iostream>
#include "sender.h"
#include "receiver.h"
#include "hub_drone.h"
#include "time.h"
#include "cmath" // for min function.
#include "limits.h" // for int_min use
#include <algorithm> // for sorting purpose

using namespace std;
pthread_t*  receiver_threads;
pthread_t*  sender_threads;
pthread_t*  drone_threads;
pthread_t*  hub_threads;

int *numHubs, *numDrones, quittedDrones=0;  // heap vars
sem_t *sQuittedDrones;
vector<Hub*> hubs;
vector<Sender*> senders;
vector<Receiver*> receivers;
vector<Drone*> drones; // heap vars accessible by class files (all threads)

pthread_cond_t barrierThreads;
pthread_mutex_t  mutexBarrier;


void inputScanner(){
    int i, j;
    numHubs =  new int;
    scanf(" %d\n", numHubs);

    hubs.reserve(*numHubs);
    senders.reserve(*numHubs);
    receivers.reserve(*numHubs);

    for( i=0; i<*numHubs; i++){    // hub properties.
        int iH, oH, cH;
        cin >> iH >> oH >> cH;
        int *distances = new int [*numHubs];
        for(j=0 ; j< *numHubs; j++){ cin >> distances[j];}
        Hub *currHub = new Hub(i+1, iH, oH, cH, distances, *numHubs);
        while(cH) {sem_post(currHub->semptyCharging); cH--;}
        hubs.push_back(currHub);
    }
    for( i=0; i<*numHubs; i++){    // sender properties.
        int sS, hS, tS; // time between two send, hub id, total packages
        cin >> sS >> hS >> tS;
        Sender *currSender = new Sender(i+1, tS, hS, sS); // id, total packages, hub id, sleep dur.
        senders.push_back(currSender);
        hubs[hS-1]->senderID= i+1;
    }
    for( i=0; i<*numHubs; i++){    // receiver properties.
        int hR, sR; // hub id, sleep duration
        cin >> sR >> hR ;
        Receiver *currReceiver = new Receiver(i+1, sR, hR);
        receivers.push_back(currReceiver);
        hubs[hR-1]->receiverID= i+1;
    }
    numDrones =  new int;
    scanf(" %d\n", numDrones);
    drones.reserve(*numDrones);

    for( i=0; i<*numDrones; i++){    // drone properties.
        int sD, hD, rD; // speed, departure hub, range
        cin >> sD >> hD >> rD ;
        Drone *currDrone = new Drone(i+1,  sD,  hD,  rD, *numHubs);
        //cout << "numhubs is " << *numHubs << endl;

        sem_wait(hubs[hD-1]->semptyCharging); // consume 1 empty place
        sem_wait(hubs[hD-1]->smutexCharging);
            hubs[hD-1]->chargingDrones.push_back(currDrone);
        sem_post(hubs[hD-1]->smutexCharging);
        drones.push_back(currDrone);
    }
}
/*
 *              SENDER
 *
 */
void Sender::WaitCanDeposit(){ // Wait until there is outgoing storage space available on the hub.
    Hub *assignedH = hubs[(this->assignedHub)-1];

    // wait empty, if enough space (if a drone signals to hub to receive) then hub signals to sender.
    sem_wait(assignedH->semptyOutgoing);
}
void Sender::SenderDeposit(Package* pack){
    Hub *hubToDepositPackage = hubs[(pack->packageInfo->sending_hub_id)-1];

    sem_wait(hubToDepositPackage->smutexOutgoing);
        hubToDepositPackage->outgoingPackages.push_back(pack); // do not lock this to make it available to picking drone.
    sem_post(hubToDepositPackage->smutexOutgoing);

    sem_post(hubToDepositPackage->sfullOutgoing); // signal one package full
}

void Sender::SenderStopped(){
        // inform all other hubs. if all senders quitted, hubs will quit in their main routine.
        for(int i=0; i<*numHubs; i++){
            sem_wait(hubs[i]->smutexSenders); // lock hub's sender count
                    (hubs[i]->numSenders)--;
                    hubs[i]->senderID = 0; // null
            sem_post(hubs[i]->smutexSenders); // unlock hub's sender count
            //cout << "sender of hub " << i << " is updated" << endl;
            //cout << "and mutex is released" << endl;
        }
}
void* Sender::senderMain(void*) {
    int selectedHubID;
    SenderInfo *senderInfo = new SenderInfo();
    FillSenderInfo(senderInfo, this->ID, this->assignedHub, this->totalPackages, NULL);
    WriteOutput(senderInfo, NULL, NULL, NULL, SENDER_CREATED);
    int remainingPackages = this->totalPackages;
    int currentHub = this->assignedHub;
    pthread_mutex_lock(&mutexBarrier);
    pthread_cond_wait(&barrierThreads, &mutexBarrier);
    pthread_mutex_unlock(&mutexBarrier);
    while(remainingPackages){
        selectedHubID=this->assignedHub;
        while(selectedHubID==this->assignedHub){        // do not pick yourself !
            selectedHubID = 1 + ( rand() % ( *numHubs - 1 + 1 ) ); // this code generates a random hub id in range
        }
        int selectedHubsReceiverID = hubs[selectedHubID-1]->receiverID; // temp val

        WaitCanDeposit(); // consumes one empty signal
        Package *package = new Package();   // one of the remaining packages
        package->packageInfo = new PackageInfo(); // needs to be released either by receiver or quitting hub.
        // first fill the info !
        FillPacketInfo(package->packageInfo, this -> ID, currentHub, selectedHubsReceiverID, selectedHubID);

        SenderDeposit(package); // deposits the package ans signals full.

        FillSenderInfo(senderInfo, this -> ID, currentHub, remainingPackages, package->packageInfo);
        WriteOutput(senderInfo, NULL, NULL, NULL, SENDER_DEPOSITED);
        sem_wait(this->smutexRemainingpackages);
            remainingPackages--;
        sem_post(this->smutexRemainingpackages);
        wait(this->sleepDuration*UNIT_TIME);
    }
    SenderStopped();
    FillSenderInfo(senderInfo, this->ID, currentHub, remainingPackages, NULL);
    WriteOutput(senderInfo, NULL, NULL, NULL, SENDER_STOPPED);
    delete senderInfo;
    return nullptr;
}

/*
 *              RECEIVER
 *
*/
void Receiver::ReceiverPickUp(Package* pack){
    //Informs the hub that the package has been picked up from the incoming package storage.
    // This means that if there are drones waiting to deliver to this hub, one of them can deliver at that point.
    Hub *hubToBeReceived = hubs[this->assignedHub-1];

    // mutex is already locked in receiverMain.
    Package *to_be_deleted = hubToBeReceived->incomingPackages[0];
    delete to_be_deleted;
    to_be_deleted= nullptr;
    pack = nullptr;
    hubToBeReceived->incomingPackages.erase(hubToBeReceived->incomingPackages.begin()+0); // remove from vector.

    sem_post(hubToBeReceived->semptyIncoming);
}

void* Receiver::receiverMain(void*) {
    ReceiverInfo *receiverInfo = new ReceiverInfo;
    FillReceiverInfo(receiverInfo, this->ID, this->assignedHub, NULL);
    WriteOutput(NULL, receiverInfo, NULL, NULL, RECEIVER_CREATED);
    Hub *assignedH = hubs[this->assignedHub-1];
    pthread_mutex_lock(&mutexBarrier);
    pthread_cond_wait(&barrierThreads, &mutexBarrier);
    pthread_mutex_unlock(&mutexBarrier);
    while (1){
        sem_wait(sQuittedDrones);
        if( quittedDrones == *numDrones)  { sem_post(sQuittedDrones); break;}
        sem_post(sQuittedDrones);
        sem_wait(assignedH->smutexIncoming); // lock vector
        if(assignedH->incomingPackages.size()){
            sem_wait(assignedH->sfullIncoming); // consume this here.
            Package *pack = (assignedH->incomingPackages)[0];
            PackageInfo *packInfoAfterDelete = new PackageInfo();
            FillPacketInfo(packInfoAfterDelete, pack->packageInfo->sender_id, pack->packageInfo->sending_hub_id, pack->packageInfo->receiver_id, this->assignedHub);
            ReceiverPickUp(pack); // package cannot be used anymore, pack=NULL. !!!
            FillReceiverInfo(receiverInfo, this->ID, this->assignedHub, packInfoAfterDelete);
            WriteOutput(NULL, receiverInfo, NULL, NULL, RECEIVER_PICKUP);
            delete packInfoAfterDelete;
            packInfoAfterDelete = nullptr;wait(this->sleepDuration*UNIT_TIME);
        }
        sem_post(assignedH->smutexIncoming);
    }

    FillReceiverInfo(receiverInfo, this->ID, this->assignedHub, NULL);
    WriteOutput(NULL, receiverInfo, NULL, NULL, RECEIVER_STOPPED);
    delete receiverInfo;
    receiverInfo= nullptr;
    return nullptr;
}

/*
 *              HUB
 *
*/

int Hub::WaitUntilPackageDeposited(){
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    ts.tv_sec += UNIT_TIME * 1/1000;
    if(sem_timedwait(this->sfullOutgoing, &ts)==-1) // semaphore for i'm waiting a sender to deposit
    {
        return 1;
    }
    else{
        //printf("I %d waited a while to see if a sender  package and it came.", this->ID);
        return 0;
    }
}
void Hub::AssignAndNotifyDrone(Package* pack, Drone *drone){ //Assign the package. After that, notify the drone for delivery.
    // charging mutex is already locked in main.

    sem_wait(hubs[pack->packageInfo->receiving_hub_id-1]->smutexActive);
    hubs[pack->packageInfo->receiving_hub_id-1]->waitingDrone++;
    sem_post(hubs[pack->packageInfo->receiving_hub_id-1]->smutexActive);

    sem_wait(drone->smutexAssignPackage);
        drone->assignedPackage = pack;
        drone->carryingPackage = 1;
    sem_post(drone->smutexAssignPackage);
    sem_post(drone->sCarryingPackage); // signal the drone for take off.
    sem_post(this->smutexOutgoing);
    sem_post(this->smutexCharging); // release before signalling drone. drone blocks on the below semaphpre and then uses these.

}
bool sortbyDistance( pair<int,int> a,  pair<int,int> b){ return (a.second < b.second);}
bool Hub::CallDroneFromHubs(){ //Starting from the closest hub to the farthest, call a drone until one is found.
    vector<pair<int, int>> idDistList;
    int i;
    bool called=0;
    for( i=0; i<this->totalSenders; i++){      // since no hubs can exit if i can not exit (i have a sender in global)
        pair<int, int> temp(i+1, this->distancesToHubs[i]);
        idDistList.push_back(temp);
    }
    sort(idDistList.begin(), idDistList.end(), sortbyDistance); // to start from closest hubs.
    struct timespec ts;
    for( i=1; i<this->totalHubCount; i++){      // no need to protect totalHubCount as local, and hub has 0 distance to itself !
        if(!called) {
            //sem_wait(hubs[idDistList[i].first - 1]->smutexCharging);
            if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
                perror("clock_gettime");
                exit(EXIT_FAILURE);
            }
            ts.tv_sec += UNIT_TIME * 1/100000;
            if(sem_timedwait(hubs[idDistList[i].first - 1]->smutexCharging, &ts)==-1) // lock the charging vector from change. executes if when not acquired
            {
                return 0;
            }
            if (hubs[idDistList[i].first - 1]->chargingDrones.size()) {
                int currange = INT_MIN;         //select the max currange drone and call it.
                int currangeIndex = -1;
                for (long unsigned int j = 0; j < hubs[idDistList[i].first - 1]->chargingDrones.size(); j++) {
                    sem_wait(hubs[idDistList[i].first - 1]->chargingDrones[j]->smutexAssignPackage);
                    if((hubs[idDistList[i].first - 1]->chargingDrones[j]->called) && hubs[idDistList[i].first - 1]->chargingDrones[j]->callingHubID==this->ID){ // i already called a drone, do not call one more excessively
                        sem_post(hubs[idDistList[i].first - 1]->chargingDrones[j]->smutexAssignPackage);
                        sem_post(hubs[idDistList[i].first - 1]->smutexCharging);
                        return 0;
                    }

                    if (!(hubs[idDistList[i].first - 1]->chargingDrones[j]->called) && !(hubs[idDistList[i].first - 1]->chargingDrones[j]->assignedPackage)) {

                        sem_wait(hubs[idDistList[i].first - 1]->chargingDrones[j]->smutexLandingTime);
                        long long howLongItHasLanded = (timeInMilliseconds()) - (hubs[idDistList[i].first - 1]->chargingDrones[j]->landingTime); // curr time - last time
                        hubs[idDistList[i].first - 1]->chargingDrones[j]->currRange = calculate_drone_charge(howLongItHasLanded,  hubs[idDistList[i].first - 1]->chargingDrones[j]->currRange, hubs[idDistList[i].first - 1]->chargingDrones[j]->MaximumRange);
                        hubs[idDistList[i].first - 1]->chargingDrones[j]->landingTime = timeInMilliseconds();
                        if (hubs[idDistList[i].first - 1]->chargingDrones[j]->currRange > currange) {

                            currange = hubs[idDistList[i].first - 1]->chargingDrones[j]->currRange;
                            currangeIndex = j;
                        }
                        sem_post(hubs[idDistList[i].first - 1]->chargingDrones[j]->smutexLandingTime);
                    }
                    sem_post(hubs[idDistList[i].first - 1]->chargingDrones[j]->smutexAssignPackage);
                }
                if(currangeIndex==-1){
                    sem_post(hubs[idDistList[i].first - 1]->smutexCharging);  // GO CHECK NEXT HUB !!!. release this hub's charging station.
                    continue;
                }
              //  printf("Hub %d called drone %d.\n", this->ID,hubs[idDistList[i].first - 1]->chargingDrones[currangeIndex]->ID );

                sem_wait(hubs[idDistList[i].first - 1]->chargingDrones[currangeIndex]->smutexAssignPackage);
                    hubs[idDistList[i].first - 1]->chargingDrones[currangeIndex]->called = 1; // to not call this one later during the charging.
                sem_post(hubs[idDistList[i].first - 1]->chargingDrones[currangeIndex]->smutexAssignPackage);

                sem_wait(hubs[idDistList[i].first - 1]->chargingDrones[currangeIndex]->smutexCallingHubID);
                     hubs[idDistList[i].first - 1]->chargingDrones[currangeIndex]->callingHubID = this->ID;
                sem_post(hubs[idDistList[i].first - 1]->chargingDrones[currangeIndex]->smutexCallingHubID);

                called = 1;       // to indicate main a drone found.
                sem_wait(smutexActive);
                waitingDrone++;
                sem_post(smutexActive);
                sem_post(hubs[idDistList[i].first - 1]->chargingDrones[currangeIndex]->sCalled); // call the drone.
                sem_post(hubs[idDistList[i].first - 1]->smutexCharging); // FORGOT TO RELEASE HERE

                break;
            }
            sem_post(hubs[idDistList[i].first - 1]->smutexCharging);
        }
        else break;
    }
    return called;
}

void Hub::WaitTimeoutOrDrone(){ // any drone can arrive, so does it have to be the one i called ???
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    ts.tv_sec += UNIT_TIME * 1/1000;
    if(sem_timedwait(this->sfullCharging, &ts)==-1) // semaphore for i'm waiting a drone
    {
        return;
    }
    else{
      //  printf("I %d waited a while to see if a drone comes by WaitTimeoutOrDrone and it came.", this->ID);
    }
    fflush(stdout);
}
void Hub::HubStopped(){
    for(int i=0; i<*numDrones; i++){    // inform drones about hub count.
        sem_wait(drones[i]->smutexHubs);
            drones[i]->numHubs--;
            //cout << drones[i]->numHubs << endl;
        sem_post(drones[i]->smutexHubs);
        //cout << "Hub " << i << " is stopped and drone " << i << "is informed about it." << endl;
    }
    sem_wait(this->smutexActive);
        this->active = 0;
    sem_post(this->smutexActive);
    //cout << "hub is set to be dead" << endl;
}
void Hub::updateCharges(){ // smutexCharging is already locked before this called.
    for(long unsigned int i=0; i<this->chargingDrones.size() ; i++){
        sem_wait(this->chargingDrones[i]->smutexAssignPackage);
            if(!(this->chargingDrones[i]->called) && !(this->chargingDrones[i]->assignedPackage)){ // as in the beginning, no need to update any charge.
                sem_wait(this->chargingDrones[i]->smutexLandingTime);
                    long long howLongItHasLanded = (timeInMilliseconds()) - (this->chargingDrones[i]->landingTime); // curr time - last time
                    this->chargingDrones[i]->currRange = calculate_drone_charge(howLongItHasLanded,  this->chargingDrones[i]->currRange, this->chargingDrones[i]->MaximumRange);
                    this->chargingDrones[i]->landingTime = timeInMilliseconds(); // update last charged time as landed time.
                sem_post(this->chargingDrones[i]->smutexLandingTime);
                //cout << "new range after update charge is: " << this->chargingDrones[i]->currRange << endl;
            }
        sem_post(this->chargingDrones[i]->smutexAssignPackage);
    }
}
void* Hub::mainHub(void*) {
    HubInfo *hubInfo = new HubInfo();
    FillHubInfo(hubInfo, this->ID);
    WriteOutput(NULL, NULL, NULL, hubInfo, HUB_CREATED);
    pthread_mutex_lock(&mutexBarrier);
    pthread_cond_wait(&barrierThreads, &mutexBarrier);
    pthread_mutex_unlock(&mutexBarrier);
    bool called=0, calledWhileCharging = 0;
    while(this->numSenders || this->incomingPackages.size() || this->outgoingPackages.size() ) { // numSenders are global senders
        if(this->senderID && !WaitUntilPackageDeposited()) { continue;} // Waits until a package is deposited from its alives sender.
        for (int start = 0; start < 1; start++) {     //   START LOOP
            if(!this->numSenders && !this->incomingPackages.size() && !this->outgoingPackages.size()) { break;} // if a hub gets a drone after call below, do not let it block from quitting.
            else if(this->chargingDrones.size() && !this->outgoingPackages.size()) {  sem_wait(this->smutexCharging); updateCharges(); sem_post(this->smutexCharging); break;}
            else if (this->chargingDrones.size() && this->outgoingPackages.size()) {
                //printf("i found a drone now. try1 %d.\n", this->chargingDrones[0]->ID);
                // update charges of drones
                if (called) {   // if this becomes false negative, below, waitingDrones notices and does not call one extra drone
                    sem_wait(this->smutexCharging);
                    sem_wait(this->smutexOutgoing);
                    called = 0;
                    int assign=0;
                    if(outgoingPackages.size()==0){ sem_post(this->smutexOutgoing); sem_post(this->smutexCharging); continue;}
                    for ( ; assign < outgoingPackages.size(); assign++) { // assign the first unassigned package to found available drone
                        if (outgoingPackages[assign]->assigned == 0) {
                            this->outgoingPackages[assign]->assigned = 1;
                            break;
                        }
                    }
                    if(assign < outgoingPackages.size()) // assign=size means a package not assigned is not found
                    {
                        AssignAndNotifyDrone(this->outgoingPackages[assign],
                                             (this->chargingDrones)[0]); // pick the first package to assign
                        continue;
                    }
                    else{
                        sem_post(this->smutexOutgoing);
                        sem_post(this->smutexCharging);
                        continue;
                    }
                }
                sem_wait(this->smutexCharging);
                updateCharges();
                int highestRange = -1;
                int highestIndex = -1;
                for (long unsigned int i = 0; i < this->chargingDrones.size(); i++) {
                    sem_wait(this->chargingDrones[i]->smutexAssignPackage);
                    if (!(((this->chargingDrones)[i])->called) && !(((this->chargingDrones)[i])->assignedPackage)) {
                        sem_wait(this->chargingDrones[i]->smutexLandingTime);
                        if(((this->chargingDrones)[i])->currRange > highestRange) {
                            highestRange = ((this->chargingDrones)[i])->currRange;
                            highestIndex = i;
                        }
                        sem_post(this->chargingDrones[i]->smutexLandingTime);
                    }
                    sem_post(this->chargingDrones[i]->smutexAssignPackage);
                }
                if (highestIndex == -1 ) { // i have drones but all are busy and reserved for some job
                    sem_wait(this->smutexActive);
                    if( chargingSize == chargingDrones.size() || (chargingSize==chargingDrones.size()+waitingDrone)) {start--;  sem_post(this->smutexActive); sem_post(this->smutexCharging); continue;} // in case full charging size and no calling possible.
                    if (*numDrones > 1) {
                        if(waitingDrone){
                            sem_post(this->smutexActive);
                            sem_post(this->smutexCharging);
                            WaitTimeoutOrDrone();
                            start--;            //    goto start
                            continue;
                        }
                        sem_post(this->smutexActive);
                        calledWhileCharging = CallDroneFromHubs();
                        if (!calledWhileCharging) {
                            //  printf("Hub %d could not find any drone to assign in this hub.\n", this->ID);
                            sem_post(this->smutexCharging);
                            WaitTimeoutOrDrone();
                            start--;            //    goto start
                            continue;
                        }
                        sem_post(this->smutexCharging);
                        continue;
                    }
                    sem_post(this->smutexActive);
                    sem_post(this->smutexCharging);
                    continue;
                }

                sem_wait(this->smutexOutgoing);
                int assign2=0;
                if(outgoingPackages.size()==0){ sem_post(this->smutexOutgoing); sem_post(this->smutexCharging); continue;}
                for (; assign2 < outgoingPackages.size(); assign2++) { // assign the first unassigned package to found available drone
                    if (outgoingPackages[assign2]->assigned == 0) {
                        this->outgoingPackages[assign2]->assigned = 1;
                        break;
                    }
                }
                if(assign2 < outgoingPackages.size()) // assign=size means a package not assigned is not found
                {
                    AssignAndNotifyDrone(this->outgoingPackages[assign2],
                                         (this->chargingDrones)[highestIndex]); // pick the first package to assign
                }
                 else{
                    sem_post(this->smutexOutgoing);
                    sem_post(this->smutexCharging);
                 }
            }
            else if(this->outgoingPackages.size()) {
                sem_wait(this->smutexActive);
                if(waitingDrone){
                    sem_post(this->smutexActive);
                    WaitTimeoutOrDrone();
                    start--;
                    continue;
                }
                sem_post(this->smutexActive);

                sem_wait(this->smutexCharging);
                sem_wait(this->smutexActive);
                if( (chargingSize == chargingDrones.size()) || (chargingDrones.size()+ waitingDrone == chargingSize)) {start--; sem_post(this->smutexActive); sem_post(this->smutexCharging); continue;} // in case full charging size and no calling possible.
                sem_post(this->smutexActive);
                called = CallDroneFromHubs();
                if (!called) {
                    sem_post(this->smutexCharging);
                    WaitTimeoutOrDrone();
                    start--;
                    continue;
                }
                sem_post(this->smutexCharging);
            }
        }
    }
    HubStopped();
    FillHubInfo(hubInfo, ID);
    WriteOutput(NULL, NULL, NULL, hubInfo, HUB_STOPPED);
    delete hubInfo;
    return nullptr;
}
/*
 *              DRONE
 *
*/
int Drone::WaitSignalFromHub(){
    int assigned=0;
    if(!sem_trywait(this->sCarryingPackage)){
            assigned=1;
    }
    else{
        if(!sem_trywait(this->sCalled)){
            assigned=0;
        }
        else {
            assigned = 2;
        }
    }
    return assigned;
}
void Drone::WaitAndReserveDestinationSpace(int DestinationHubID){
    Hub *destHub = hubs[DestinationHubID-1];
    //sem_wait(destHub->smutexActive);
    //destHub->waitingDrone++;         // this is only to block receiver quitting immediately when hub terminates
    //sem_post(destHub->smutexActive);
    sem_wait(destHub->semptyCharging); // wait charging space.
    sem_wait(destHub->semptyIncoming); // wait for a place to drop package.
    sem_post(destHub->sfullIncoming);
    sem_post(destHub->sfullCharging); // reserve the space for the drone.
}
int Drone::WaitForRange(int DestinationHubID){ // in case some time passed, re update the current range before charging.
    Hub *currHub = hubs[this->StartingHub-1];
    sem_wait(this->smutexLandingTime); // first update the charge since last landed, then decide whether it needs to wait a charge.
        long long howLongItHasLanded = (timeInMilliseconds()) - (this->landingTime); // curr time - last time
        this->currRange = calculate_drone_charge(howLongItHasLanded,  this->currRange, this->MaximumRange);
        this->landingTime = timeInMilliseconds(); // update last charged time as landed time to prevent it being incremented excessively.
    int travelDist = currHub->distancesToHubs[DestinationHubID-1];
    int wastedRange = range_decrease(travelDist,  this->Speed);
    if(this->currRange < wastedRange && wastedRange <= this->MaximumRange ){ // equality means no need for a charge.
        int neededChargeUnits = wastedRange - this->currRange;
        wait(neededChargeUnits * UNIT_TIME); // sleep until i.e: 5 unit time charge becomes full.
        this->currRange = fmin(this->currRange + neededChargeUnits, this->MaximumRange); // update new charge upon quitting.
        sem_post(this->smutexLandingTime);
        return 0;
    }
    else if(wastedRange > this->MaximumRange  ){
       // printf("This travel I cannot afford due to my max range !!!\n");
        sem_post(this->smutexLandingTime);
        return 8;
    }
    //printf("I already have enough charge.\n");
    sem_post(this->smutexLandingTime);
    return 0;
}
int Drone::TakePackageFromHub(Package* pack){
    if(pack){
        bool removed=0;
        Hub *currHub = hubs[this->StartingHub-1];
        sem_wait(currHub->smutexCharging);
             sem_wait(currHub->smutexOutgoing);

            for(long unsigned int i=0; i<currHub->outgoingPackages.size(); i++){
                if(currHub->outgoingPackages[i]==pack){
                    currHub->outgoingPackages.erase(currHub->outgoingPackages.begin()+i); // do not delete the package entirely.
                    sem_post(currHub->semptyOutgoing); // one package has gone, inform local sender.
                    removed = 1;
                    break;
                }
            }
                for(long unsigned int i=0; i<currHub->chargingDrones.size(); i++){
                    if(currHub->chargingDrones[i]==this){
                        currHub->chargingDrones.erase(currHub->chargingDrones.begin()+i);
                        sem_post(currHub->semptyCharging); // i took off.
                        removed = 1;
                        break;
                    }
                }


             sem_post(currHub->smutexOutgoing);
        sem_post(currHub->smutexCharging);

        if(removed) return 0;
        else{
         //   printf("Cannot find the package assigned to me to take off. !!!!\n");
            return 88;
        }
    }
    else{
       // printf("I am not assigned a package at all !!!!\n");
        return 1;
    }

}
int Drone::DropPackageToHub(Package* pack){
    if(pack){
        Hub *currHub = hubs[this->StartingHub-1];

        sem_wait(currHub->smutexCharging);
        sem_wait(currHub->smutexIncoming);
            currHub->incomingPackages.push_back(pack); // handled during reservation.
            currHub->chargingDrones.push_back(this);    // i landed on the place i already reserved with a signal. no more signaling thats why !.
        sem_post(currHub->smutexIncoming);
        sem_post(currHub->smutexCharging);

        sem_wait(this->smutexLandingTime);
            this->landingTime=timeInMilliseconds(); // now
        sem_post(this->smutexLandingTime);
        sem_wait(this->smutexAssignPackage);
            this->assignedPackage = nullptr;
            pack->assigned = 0; // EMPTY YOUR ASSIGNMENT AFTER THE PACKAGE INFO PRINT !
            this->called =  this->carryingPackage  =0;
        sem_post(this->smutexAssignPackage);


        sem_wait(currHub->smutexActive);
        currHub->waitingDrone--;
        sem_post(currHub->smutexActive);
        return 0;
    }
    else{
      //  printf("I am not assigned a package at all so I cannot deliver. !!!!\n");
        return 1;
    }
}
void Drone::WaitAndReserveChargingSpace(int callingHubID){
    Hub *destHub = hubs[callingHubID-1];
    //sem_wait(destHub->smutexActive);
   // destHub->waitingDrone++;
   // sem_post(destHub->smutexActive);
    sem_wait(destHub->semptyCharging); // consume "emptied charging" signal, necessary.
    sem_post(destHub->sfullCharging); // reserve the space for the drone.
}
void* Drone::mainDrone(void*) {
    DroneInfo *droneInfo = new DroneInfo();
    FillDroneInfo(droneInfo, this->ID, this->StartingHub, this->currRange, NULL, 0); // next hub=0 when no packages are carried.
    WriteOutput(NULL, NULL, droneInfo, NULL, DRONE_CREATED);
    pthread_mutex_lock(&mutexBarrier);
    pthread_cond_wait(&barrierThreads, &mutexBarrier);
    pthread_mutex_unlock(&mutexBarrier);

    while(this->numHubs) {
        int assigned = WaitSignalFromHub();
        if(assigned==1){
            if(!this->assignedPackage) {
                printf("i'm not assigned indeed?\n");
                return nullptr;
            }
            WaitAndReserveDestinationSpace(this->assignedPackage->packageInfo->receiving_hub_id);
            WaitForRange(this->assignedPackage->packageInfo->receiving_hub_id);
            if(TakePackageFromHub(this->assignedPackage)) {cout << "ERROR" << endl; exit(7);}
            FillDroneInfo(droneInfo, this->ID, this->StartingHub, this->currRange, this->assignedPackage->packageInfo, 0);
            WriteOutput(NULL, NULL, droneInfo, NULL, DRONE_PICKUP);
            int travelDist = hubs[this->StartingHub-1]->distancesToHubs[this->assignedPackage->packageInfo->receiving_hub_id-1];
            travel(travelDist, this->Speed);
            sem_wait(this->smutexLandingTime); // this lock is available for other operations as well.
                this->currRange = this->currRange - (travelDist/(this->Speed));
                this->StartingHub =  this->assignedPackage->packageInfo->receiving_hub_id; // update new hub as starting.
            sem_post(this->smutexLandingTime);
            FillPacketInfo(this->assignedPackage->packageInfo, this->assignedPackage->packageInfo->sender_id, this->assignedPackage->packageInfo->sending_hub_id, this->assignedPackage->packageInfo->receiver_id, this->StartingHub);
            FillDroneInfo(droneInfo, this->ID, this->StartingHub, this->currRange, this->assignedPackage->packageInfo, 0);
            WriteOutput(NULL, NULL, droneInfo, NULL, DRONE_DEPOSITED);
            DropPackageToHub(this->assignedPackage);
        }
        else if(!assigned){
            WaitAndReserveChargingSpace(this->callingHubID);
            if (WaitForRange(this->callingHubID)) {cout << "ERROR" << endl; exit(8);}
            Hub* currHub = hubs[this->StartingHub-1];
            sem_wait(currHub->smutexCharging);
            for(long unsigned int i=0; i<currHub->chargingDrones.size(); i++){
                if(currHub->chargingDrones[i]==this){
                    currHub->chargingDrones.erase(currHub->chargingDrones.begin()+i);
                    sem_post(currHub->semptyCharging); // i took off.
                    break;
                }
            }
            sem_post(currHub->smutexCharging);
            FillDroneInfo(droneInfo, this->ID, this->StartingHub, this->currRange, NULL, this->callingHubID );
            WriteOutput(NULL, NULL, droneInfo, NULL, DRONE_GOING);
            int travelDist = hubs[this->StartingHub-1]->distancesToHubs[this->callingHubID -1];
            travel(travelDist, this->Speed);
            sem_wait(hubs[this->callingHubID-1]->smutexCharging);
                hubs[this->callingHubID-1]->chargingDrones.push_back(this);    // i landed on the place i already reserved with a signal. no more signaling thats why !.
                sem_wait(this->smutexLandingTime);
                    this->currRange = this->currRange - (travelDist/(this->Speed));
                    this->StartingHub = this->callingHubID; // update new hub as starting.
                    this->landingTime= timeInMilliseconds();
                sem_post(this->smutexLandingTime);
                sem_wait(this->smutexAssignPackage);
                    this->called = this->carryingPackage = 0;
                    this->assignedPackage = nullptr;
                sem_post(this->smutexAssignPackage);
            sem_post(hubs[this->callingHubID-1]->smutexCharging);
            sem_wait(hubs[this->StartingHub-1]->smutexActive);
            hubs[this->StartingHub-1]->waitingDrone--;
            sem_post(hubs[this->StartingHub-1]->smutexActive);
            FillDroneInfo(droneInfo, this->ID, this->StartingHub, this->currRange, NULL, 0);
            WriteOutput(NULL, NULL, droneInfo, NULL, DRONE_ARRIVED);
        }
    }
    FillDroneInfo(droneInfo, this->ID, this->StartingHub, this->currRange, NULL, 0);
    WriteOutput(NULL, NULL, droneInfo, NULL, DRONE_STOPPED);
    sem_wait(sQuittedDrones);
    quittedDrones++;
    sem_post(sQuittedDrones);
    return nullptr;
}

int main(){
    sQuittedDrones = new sem_t ;
    if (sem_init(sQuittedDrones, 0, 1)) {
        perror("Failed to open drone semphore for quitting");
    }
    mutexBarrier = PTHREAD_MUTEX_INITIALIZER;
    barrierThreads = PTHREAD_COND_INITIALIZER;
    inputScanner();
    InitWriteOutput(); //initialize the time
    int i, err1, err2, err3;

    /*          THREAD INITIALIZATIONS HERE           */
    hub_threads =  new pthread_t [*numHubs];
    sender_threads =  new pthread_t [*numHubs];
    receiver_threads =  new pthread_t [*numHubs];
    drone_threads =  new pthread_t [*numDrones];
    for( i=0; i< *numHubs; i++){
        pthread_t h, s , r;
        hub_threads[i] = h;
        err1 = pthread_create( &hub_threads[i], NULL, &Hub::send_wrapper, hubs[i]);
        if(err1)
        {
            //fprintf(stderr,"Error - pthread_create() hub error returned: %d\n", err1);
            exit(EXIT_FAILURE);
        }
        sender_threads[i] = s;
        err2 = pthread_create( &sender_threads[i], NULL, &Sender::send_wrapper, senders[i]);
        if(err2)
        {
          //  fprintf(stderr,"Error - pthread_create() sender error returned: %d\n", err2);
            exit(EXIT_FAILURE);
        }
        receiver_threads[i] = r;
        err3 = pthread_create( &receiver_threads[i], NULL, &Receiver::send_wrapper, receivers[i]);
        if(err3)
        {
            //fprintf(stderr,"Error - pthread_create() sender error returned: %d\n", err3);
            exit(EXIT_FAILURE);
        }
    }
    for( i=0; i< *numDrones; i++){
        pthread_t d;
        drone_threads[i] = d;
        err1 = pthread_create( &drone_threads[i], NULL, &Drone::send_wrapper, drones[i]);
        if(err1)
        {
           // fprintf(stderr,"Error - pthread_create() hub error returned: %d\n", err1);
            exit(EXIT_FAILURE);
        }
    }
    sleep(1); // wait all threads to reach the barrier, just to observe things.
    pthread_mutex_lock(&mutexBarrier);
    pthread_cond_broadcast(&barrierThreads);
    pthread_mutex_unlock(&mutexBarrier);
    /*          THREAD JOINS HERE           */
    for(i=0; i < *numHubs; i++)
    {
        pthread_join( hub_threads[i], NULL); // join hub threads
        pthread_join( sender_threads[i], NULL); // join sender threads
        pthread_join( receiver_threads[i], NULL); // join receiver threads
    }
    for(i=0; i < *numDrones; i++)
    {
        pthread_join( drone_threads[i], NULL); // join drone threads
    }
    delete [] drone_threads;
    delete [] hub_threads;
    delete [] sender_threads;
    delete [] receiver_threads;

    //printf("all threads have successfully completed.\n");
    long unsigned int  j;
    for(int i=0; i< *numHubs; i++){
            if(hubs[i]){
                //if(hubs[i]->distancesToHubs) delete [] hubs[i]->distancesToHubs;

                for(j=0; j<hubs[i]->incomingPackages.size(); j++){ // free left pckgs if any
                    if(hubs[i]->incomingPackages[j]){
                        delete hubs[i]->incomingPackages[j];
                        hubs[i]->incomingPackages[j] = nullptr;
                    }
                }
                for(j=0; j<hubs[i]->outgoingPackages.size(); j++){ // free left pckgs if any
                    if(hubs[i]->outgoingPackages[j]){
                        delete hubs[i]->outgoingPackages[j];
                        hubs[i]->outgoingPackages[j] = nullptr;
                    }
                }
                for(j=0; j<hubs[i]->chargingDrones.size(); j++){ // free left drones if any
                    if(hubs[i]->chargingDrones[j]){
                        hubs[i]->chargingDrones[j] = nullptr;   // only prevent dangling reference here, delete below later on.
                    }
                }
                if (sem_destroy(hubs[i]->sfullIncoming)) {
                    perror("Failed to destroy semphore");
                }
                if (sem_destroy(hubs[i]->smutexIncoming)) {
                    perror("Failed to destroy semphore");
                }
                if (sem_destroy(hubs[i]->semptyIncoming)) {
                    perror("Failed to destroy semphore");
                }
                if (sem_destroy(hubs[i]->sfullOutgoing)) {
                    perror("Failed to destroy semphore");
                }
                if (sem_destroy(hubs[i]->smutexOutgoing)) {
                    perror("Failed to destroy semphore");
                }
                if (sem_destroy(hubs[i]->semptyOutgoing)) {
                    perror("Failed to destroy semphore");
                }
                if (sem_destroy(hubs[i]->sfullCharging)) {
                    perror("Failed to destroy semphore");
                }
                if (sem_destroy(hubs[i]->smutexCharging)) {
                    perror("Failed to destroy semphore");
                }
                if (sem_destroy(hubs[i]->semptyCharging)) {
                    perror("Failed to destroy semphore");
                }
                if (sem_destroy(hubs[i]->smutexSenders)) {
                    perror("Failed to destroy semphore");
                }
                if (sem_destroy(hubs[i]->smutexActive)) {
                    perror("Failed to destroy semphore");
                }
                delete hubs[i];
                hubs[i] = nullptr;
            }
    }
    for(int i=0; i< *numHubs; i++){
        if(senders[i]){
            if (sem_destroy(senders[i]->smutexRemainingpackages)) {
                perror("Failed to destroy semphore");
            }
            delete senders[i];
            senders[i] = nullptr;
        }
    }
    for(int i=0; i< *numHubs; i++){
        if(receivers[i]){
            delete receivers[i];
            receivers[i] = nullptr;
        }
    }
    for(int i=0; i< *numDrones; i++){
        if(drones[i]){
            if (sem_destroy(drones[i]->sCarryingPackage)) {
                perror("Failed to destroy drones semphore");
            }
            if (sem_destroy(drones[i]->smutexAssignPackage)) {
                perror("Failed to destroy drones semphore");
            }
            if (sem_destroy(drones[i]->sCalled)) {
                perror("Failed to destroy drones semphore");
            }
            if (sem_destroy(drones[i]->smutexHubs)) {
                perror("Failed to destroy drones semphore");
            }
            if (sem_destroy(drones[i]->smutexCharging)) {
                perror("Failed to destroy drones semphore");
            }
            if (sem_destroy(drones[i]->smutexLandingTime)) {
                perror("Failed to destroy drones semphore");
            }
            delete drones[i];
            drones[i] = nullptr;
        }
    }
    if (sem_destroy(sQuittedDrones)) {
        perror("Failed to destroy sQuittedDrones semphore");
    }
    delete numHubs;
    numHubs = nullptr;
    delete numDrones;
    numDrones= nullptr;
    exit(2);
}