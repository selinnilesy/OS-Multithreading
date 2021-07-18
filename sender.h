//
// Created by Selin Yıldırım on 14.05.2021.
//

#ifndef SOURCE_SENDER_H
#define SOURCE_SENDER_H
#include "monitor.h"
#include "package.h"

class Sender: public Monitor {

// when following is used as a local variable the method becomes a monitor method. On constructor
// lock is acquired, when function returns, automatically called desctructor unlocks it.
//#define __synchronized__ Lock mutex(this);
public:

    int ID, totalPackages, assignedHub, sleepDuration;
    sem_t *smutexRemainingpackages; // protects remainingPackages in the Main Routine .
    // pthread_mutex_t smutexLeftPackages = PTHREAD_MUTEX_INITIALIZER; // protects change on totalPackages
    // to use the mutex, say pthread_mutex_lock( &mutex1 ) and pthread_mutex_unlock( &mutex1 ).

    Sender(int ID,  int totalPackages, int assignedHub, int sleepDuration)
            : ID(ID), totalPackages(totalPackages),
              assignedHub(assignedHub), sleepDuration(sleepDuration) // pass "this" to cv constructors
    {
        smutexRemainingpackages = new sem_t();
        if (sem_init(smutexRemainingpackages, 0, 1)) {
            perror("Failed to open drone semphore for empty");
        }
    }
    void WaitCanDeposit();
    void SenderDeposit(Package*);
    void SenderStopped();
    void* senderMain(void*);
    static void* send_wrapper(void* object)
    {
        reinterpret_cast<Sender*>(object)->senderMain(nullptr);
        return 0;
    }
};


#endif //SOURCE_SENDER_H
