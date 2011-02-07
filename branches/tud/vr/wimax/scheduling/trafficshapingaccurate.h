/*
 * trafficpolicingaccurate.h
 *
 *  Created on: 07.09.2010
 *      Author: richter
 */

#ifndef TRAFFICSHAPINGACCURATE_H_
#define TRAFFICSHAPINGACCURATE_H_

#include "trafficshapinginterface.h"
#include <map>
#include <deque>
using namespace std;

//MAKRO MIN
#define MIN(a,b) ( (a) <= (b) ? (a) : (b) )

//----------------Prediction Data-------------------------------
struct AllocationElement {
    u_int32_t mrtrSize;
    u_int32_t mstrSize;
    double timeStamp;
};
struct AllocationList {
    u_int32_t sumMrtrSize;
    u_int32_t sumMstrSize;
    deque <AllocationElement> *ptrDequeAllocationElement;
};
//-------------------------deque < TrafficRate > dequeTrafficRate-----------------------------------------;
//----Abbildungen von cid auf Prediction Data heiß  LastAllocationSize(mrtrSize; mstrSize;timeStamp;)------------

typedef map< int, AllocationList > MapLastAllocationList_t;
typedef MapLastAllocationList_t::iterator AllocationListIt_t;

class Connection;

class TrafficShapingAccurate : public TrafficShapingInterface
{
public:
    TrafficShapingAccurate(double frameDuration_);
    virtual ~TrafficShapingAccurate();

    /*
     * Calculate predicted mrtr and msrt sizes
     */
    virtual MrtrMstrPair_t getDataSize(Connection* connection);

    /*
     * Sends occurred allocation back to traffic policing
     */
    virtual void updateAllocation(Connection* connection,u_int32_t mstrSize,u_int32_t mrtSize);
private :
    /*
     * Saves the last occurred allocations
     */
    MapLastAllocationList_t mapLastAllocationList_ ;
};

#endif /* TRAFFICPOLICINGACCURATE_H_ */