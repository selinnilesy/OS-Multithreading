# OS-Multithreading <br />
Drone Simulation homework of CENG334, June 2021 <br />

Hubs contain charging spaces for drones and incoming/outgoing storage spaces for senders and receivers. <br />
Senders can deposit and receivers can take their delivery while drones are depositing packages to them simultaneously.  <br />
Drones are allowed to be called from idle hubs in addition to being assigned to a package by its hub. <br />
A drone shall wait before takeoff until its charge is fulled to an amount of enough degree for its assigned destination.  <br />
 <br />
 <br />
Note:  <br />
About the unpolished details; <br />
-Homework classes were intended to be implemented by a Mesa Monitor but thereafter semaphores revealed to be more useful in many aspects. Just ignore the monitor inheritance of classes since there is no condition variables declared in anyways. <br />
-Simulator.cpp contains all the member functions of simulation classes being written in a short time. Also some of the functions also implement the same functionalities of simulator multiple times for debugging purposes  <br />
and remained that way afterwards, due to lack of time.<br />
