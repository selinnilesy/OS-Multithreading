//
// Created by Selin Yıldırım on 13.05.2021.
//

#ifndef SOURCE_PACKAGE_H
#define SOURCE_PACKAGE_H

#include "writeOutput.c"
#include "helper.c"
#include <vector>           // common facilities.
#include <semaphore.h>
#include <time.h>
#include <fcntl.h>           /* For O_* constants */
#include <sys/stat.h>        /* For mode constants */
using namespace std;

typedef struct Package {
    PackageInfo *packageInfo;
    int assigned=0;
} Package;

#endif //SOURCE_PACKAGE_H
