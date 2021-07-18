//
// Created by Selin Yıldırım on 14.05.2021.
//

#ifndef SOURCE_RECEIVER_H
#define SOURCE_RECEIVER_H
#include "monitor.h"
#include "package.h"

class Receiver: public Monitor {

// when following is used as a local variable the method becomes a monitor method. On constructor
// lock is acquired, when function returns, automatically called desctructor unlocks it.
//#define __synchronized__ Lock mutex(this);
public:
    int ID, sleepDuration, assignedHub;

    Receiver(int ID, int sleepDuration, int assignedHub) // pass "this" to cv constructors
    {
        this->ID = ID;
        this->sleepDuration = sleepDuration;
        this->assignedHub = assignedHub;
    }
    void ReceiverPickUp(Package*);
    void* receiverMain(void*);
    static void* send_wrapper(void* object)
    {
        reinterpret_cast<Receiver*>(object)->receiverMain(nullptr);
        return 0;
    }
};


#endif //SOURCE_RECEIVER_H
