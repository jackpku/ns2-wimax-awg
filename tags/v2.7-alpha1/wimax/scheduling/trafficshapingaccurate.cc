/*
 * trafficshapingaccurate.cpp
 *
 *  Created on: 07.09.2010
 *      Author: tung & richter
 */

#include "trafficshapingaccurate.h"
#include "serviceflowqosset.h"
#include "serviceflow.h"
#include "connection.h"


/*
 * Timebaseboundary * FrameDuration defines the time when an deque elements is removed
 */
#define TIMEBASEBOUNDARY 0.51

TrafficShapingAccurate::TrafficShapingAccurate(double frameDuration) : TrafficShapingInterface( frameDuration)
{
    // Nothing to do
}

TrafficShapingAccurate::~TrafficShapingAccurate()
{
    // delete all deque elements
    TrafficShapingListIt_t mapIterator;
    for(mapIterator=mapLastTrafficShapingList_.begin(); mapIterator!=mapLastTrafficShapingList_.end(); mapIterator++) {
        delete mapIterator->second.ptrDequeTrafficShapingElement;
    }
}


/*
 * Calculate predicted mrtr and msrt sizes
 */
MrtrMstrPair_t TrafficShapingAccurate::getDataSizes(Connection *connection, u_int32_t queuePayloadSize)
{

	//-----------------------Initialization of the Map---------------------------------
    TrafficShapingListIt_t mapIterator;
    int currentCid = connection->get_cid();

    // get QoS Parameter
    ServiceFlowQosSet* sfQosSet = connection->getServiceFlow()->getQosSet();

    // search the current CID within the map
    mapIterator = mapLastTrafficShapingList_.find(currentCid);

    u_int32_t wantedMrtrSize;
    u_int32_t wantedMstrSize;

    if ( mapIterator == mapLastTrafficShapingList_.end()) {
        // there is no entry for this CID -> calculate default sizes

        // round up the mrtr size as it is a guaranteed rate
        wantedMrtrSize = u_int32_t( ceil( double(sfQosSet->getMinReservedTrafficRate()) * frameDuration_ / 8.0 ) );

        // round down the mstr rate as it is the upper border
        wantedMstrSize = u_int32_t( floor( double(sfQosSet->getMaxSustainedTrafficRate()) * frameDuration_/ 8.0 ) );

        // fix rounding errors
        if ( wantedMstrSize < wantedMrtrSize) {
        	wantedMstrSize = wantedMrtrSize;
        }


    } else {
    	// there is an entry for this CID

    	// short the mrtr and mstr sum to the current time

        //  get Windows Size in milliseconds from QoS Parameter Set and convert it to seconds
        double timeBase = double(sfQosSet->getTimeBase()) * 1e-3;

		// remove all element, which are older than NOW - ( Timebase - 1.5 * frameDuration)
        while ( ( ! mapIterator->second.ptrDequeTrafficShapingElement->empty()) &&
        		( mapIterator->second.ptrDequeTrafficShapingElement->front().timeStamp < ( NOW - ( timeBase - TIMEBASEBOUNDARY * frameDuration_)) )) {
			// update sum counters
			mapIterator->second.sumMrtrSize -= mapIterator->second.ptrDequeTrafficShapingElement->front().mrtrSize;
			mapIterator->second.sumMstrSize -= mapIterator->second.ptrDequeTrafficShapingElement->front().mstrSize;

			// remove oldest element
			mapIterator->second.ptrDequeTrafficShapingElement->pop_front();
        }

		// debug check for negative overflows
		assert( mapIterator->second.sumMrtrSize < 100000000);
		assert( mapIterator->second.sumMstrSize < 100000000);

		// debug
        printf("Current sumMrtrSize %d sumMstrSize %d \n", mapIterator->second.sumMrtrSize, mapIterator->second.sumMstrSize);

        // maximum data volume for mrtr allocation = Time Base size - already assigned size
        wantedMrtrSize = u_int32_t( ceil(timeBase * sfQosSet->getMinReservedTrafficRate() / 8.0) ) - mapIterator->second.sumMrtrSize;

        // maximum data volume for mstr allocation = Time Base size - already assigned size
        wantedMstrSize = u_int32_t( floor(timeBase * sfQosSet->getMaxSustainedTrafficRate() / 8.0) ) - mapIterator->second.sumMstrSize;

        // fix rounding errors
        if ( wantedMstrSize < wantedMrtrSize) {
        	wantedMstrSize = wantedMrtrSize;
        }

        assert(wantedMrtrSize <= u_int32_t( ceil(timeBase * sfQosSet->getMinReservedTrafficRate() / 8.0 )));
        assert(wantedMstrSize <= u_int32_t( floor(timeBase * sfQosSet->getMaxSustainedTrafficRate() / 8.0 ))) ;

    }

    // get Maximum Traffic Burst Size for this connection
    u_int32_t maxTrafficBurst = sfQosSet->getMaxTrafficBurst();

    //
    if ( connection->getFragmentBytes() > 0 ) {
    	queuePayloadSize -= ( connection->getFragmentBytes() - HDR_MAC802_16_SIZE - HDR_MAC802_16_FRAGSUB_SIZE );
    }

    // min function Mrtr according to IEEE 802.16-2009
    wantedMrtrSize = MIN( wantedMrtrSize, maxTrafficBurst);
    wantedMrtrSize = MIN( wantedMrtrSize, queuePayloadSize);

    // min function for Mstr
    wantedMstrSize = MIN( wantedMstrSize, maxTrafficBurst);
    wantedMstrSize = MIN( wantedMstrSize, queuePayloadSize);

    MrtrMstrPair_t mrtrMstrPair;
    mrtrMstrPair.first = wantedMrtrSize;
    mrtrMstrPair.second = wantedMstrSize;


    return mrtrMstrPair;

}

/*
 * Sends occurred allocation back to traffic policing
 */
void TrafficShapingAccurate::updateAllocation(Connection *con, u_int32_t realMrtrSize, u_int32_t realMstrSize)
{
    // create iterator
    MapLastTrafficShapingList_t::iterator mapIterator;

    // get cid of current connection
    int currentCid = con->get_cid();

    // debug
    assert(realMrtrSize <= realMstrSize);
    assert(realMstrSize <= 100000000);

    // get QoS Parameter list
    ServiceFlowQosSet* sfQosSet = con->getServiceFlow()->getQosSet();

    // get Windows Size in milliseconds from QoS Parameter Set and convert it to seconds
    double timeBase = double(sfQosSet->getTimeBase()) * 1e-3; // time base in second

    // lock for an entry for the current connection
    mapIterator = mapLastTrafficShapingList_.find(currentCid);

    if ( mapIterator == mapLastTrafficShapingList_.end()) {
    	// no entry found for this CID

    	// create a new map element and fill it with default data

    	TrafficShapingList currentAllocationList;

        // create map entry
        currentAllocationList.sumMrtrSize = realMrtrSize;
        currentAllocationList.sumMstrSize = realMstrSize;

        // create new deque
        currentAllocationList.ptrDequeTrafficShapingElement = new deque<TrafficShapingElement>;

        // create first element and append it on the back site

        TrafficShapingElement currentTrafficShapingElement;

        currentTrafficShapingElement.mrtrSize = realMrtrSize;
        currentTrafficShapingElement.mstrSize = realMstrSize;
        currentTrafficShapingElement.timeStamp = NOW;

        // save element in the deque queue
        currentAllocationList.ptrDequeTrafficShapingElement->push_front( currentTrafficShapingElement);


        // Fill Deque with default values to avoid start oscillations

        // calculate default mrtr size
        u_int32_t defaultMrtrSize = u_int32_t( ceil( double(sfQosSet->getMinReservedTrafficRate()) * frameDuration_ / 8.0 ) );

        // calculate default mstr size
        u_int32_t defaultMstrSize = u_int32_t( floor( double(sfQosSet->getMaxSustainedTrafficRate()) * frameDuration_/ 8.0 ) );

        // fix rounding errors
        if ( defaultMrtrSize > defaultMstrSize) {
        	defaultMstrSize = defaultMrtrSize;
        }


        // as long as the time base is not full , i will fill the default value into the deque
        // oldest element has to be on the front position
        double defaultTimeStamp = NOW - frameDuration_;
        while ( defaultTimeStamp >= ( NOW - ( timeBase - frameDuration_)) ) {
            // default value of the deque elements
            currentTrafficShapingElement.mrtrSize =  defaultMrtrSize;
            currentTrafficShapingElement.mstrSize =  defaultMstrSize;
            currentTrafficShapingElement.timeStamp = defaultTimeStamp;

            // add element to the front position as it is older than the first element
            currentAllocationList.ptrDequeTrafficShapingElement->push_front( currentTrafficShapingElement);

            // update sum values
            currentAllocationList.sumMrtrSize += defaultMrtrSize;
            currentAllocationList.sumMstrSize += defaultMstrSize;

            // decrease defaultTimeStamp
            defaultTimeStamp -= frameDuration_;
        }

        // debug
        printf("Current lenght %d sumMrtrSize %d sumMstrSize %d \n", int(currentAllocationList.ptrDequeTrafficShapingElement->size()), currentAllocationList.sumMrtrSize, currentAllocationList.sumMstrSize);


        // add entry in the map
        mapLastTrafficShapingList_.insert( pair<int,TrafficShapingList>( currentCid, currentAllocationList) );


    } else {
        // Entry for the current cid was found

    	// create new element for the deque queue
        TrafficShapingElement currentTrafficShapingElement;
        currentTrafficShapingElement.mrtrSize = realMrtrSize;
        currentTrafficShapingElement.mstrSize = realMstrSize;
        currentTrafficShapingElement.timeStamp = NOW;

        // add new element at the back position as it is the newest element
        mapIterator->second.ptrDequeTrafficShapingElement->push_back(currentTrafficShapingElement);

        // update sum values
        mapIterator->second.sumMrtrSize += realMrtrSize;
        mapIterator->second.sumMstrSize += realMstrSize;

        // debug
		printf("Current lenght %d sumMrtrSize %d sumMstrSize %d \n", int(mapIterator->second.ptrDequeTrafficShapingElement->size()), mapIterator->second.sumMrtrSize, mapIterator->second.sumMstrSize);
		assert(mapIterator->second.sumMrtrSize <= mapIterator->second.sumMstrSize);
    }

}

