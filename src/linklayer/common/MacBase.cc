//
// Copyright (C) 2013 OpenSim Ltd.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//
// author: Zoltan Bojthe (zbojthe@omnetpp.org)
//

#include "MacBase.h"
#include "IPassiveQueue.h"


simsignal_t MacBase::packetSentToLowerSignal = SIMSIGNAL_NULL;
simsignal_t MacBase::packetReceivedFromLowerSignal = SIMSIGNAL_NULL;
simsignal_t MacBase::packetSentToUpperSignal = SIMSIGNAL_NULL;
simsignal_t MacBase::packetReceivedFromUpperSignal = SIMSIGNAL_NULL;

void MacBase::initialize(int stage)
{
    if (stage == 0)
    {
        upperLayerIn = findGate("upperLayerIn");
        upperLayerOut = findGate("upperLayerOut");
        lowerLayerIn = findGate("lowerLayerIn");
        lowerLayerOut = findGate("lowerLayerOut");

        initializeQueueModule();

        packetSentToLowerSignal = registerSignal("packetSentToLower");
        packetReceivedFromLowerSignal = registerSignal("packetReceivedFromLower");
        packetSentToUpperSignal = registerSignal("packetSentToUpper");
        packetReceivedFromUpperSignal = registerSignal("packetReceivedFromUpper");
    }
}

void MacBase::initializeQueueModule()
{
    hasOutStandingRequest = false;
    const char *queueModuleName = par("queueModule").stringValue();
    if (queueModuleName[0])
    {
        cModule *module = getParentModule()->getSubmodule(queueModuleName);
        cModule *queueModule;
        if (module->isSimple())
            queueModule = module;
        else
        {
            cGate *queueOut = module->gate("out")->getPathStartGate();
            queueModule = queueOut->getOwnerModule();
        }

        externalQueue = check_and_cast<IPassiveQueue *>(queueModule);
        queueLimit = -1;
    }
    else
    {
        externalQueue = NULL;
        queueLimit = par("txQueueLimit").longValue();
    }
}

void MacBase::handleMessage(cMessage *msg)
{
    inside = true;   //XXX ilyennek nem szabad lennie

    if (msg->isSelfMessage())
    {
        handleSelfMsg(msg);
    }
    else if (msg->getArrivalGateId() == upperLayerIn)
    {
        if(msg->isPacket())
            emit(packetReceivedFromUpperSignal, msg);

        if (externalQueue)
        {
            hasOutStandingRequest = (externalQueue->getNumPendingRequests() > 0);
            handleMsgFromUpperQueue(msg);   //XXX
        }
        else
            pushMsgToInnerQueue(msg);
    }
    else if (msg->getArrivalGateId() == lowerLayerIn)
    {
        emit(packetReceivedFromLowerSignal, msg);
        handleMsgFromLL(msg);
    }
    else
        throw cRuntimeError("Message '%s' received on unexpected gate '%s'", msg->getName(), msg->getArrivalGate()->getFullName());

    inside = false;

    processRequestedPackets();  //XXX ????
}


void MacBase::pushMsgToInnerQueue(cMessage *msg)
{
    if (queueLimit != 0 && innerQueue.getLength() > queueLimit)
        error("txQueue length exceeds %d -- this is probably due to "
              "a bogus app model generating excessive traffic "
              "(or if this is normal, increase txQueueLimit!)",
              queueLimit);
    innerQueue.insert(msg);
}


void MacBase::processRequestedPackets()   //XXX na ilyennek egyaltalan nem kellene lennie!
{
    if (!inside && !externalQueue)
    {
        inside = true;

        while(hasOutStandingRequest && !innerQueue.empty())
        {
            hasOutStandingRequest = false;
            cObject *msg = innerQueue.pop();
            if (msg)
                handleMsgFromUpperQueue(check_and_cast<cMessage *>(msg)); //???? siman return msg!!!
        }
        inside = false;
    }
}

void MacBase::requestNextMsgFromUpperQueue() //XXX ilyen nem kellene????
{
    if (hasOutStandingRequest)
        throw cRuntimeError("Model error: already has an outstanding request");

    hasOutStandingRequest = true;
    if (externalQueue)
        externalQueue->requestPacket();
    else
        processRequestedPackets();
}

cPacket *MacBase::getPacketFromQueue()  //XXX ez lenne a fenti helyett
{
    if (externalQueue) 
    {
        ASSERT(innerQueue.isEmpty());
        externalQueue->requestPacket();
        return NULL;
    }
    else
    {
        cObject *msg = innerQueue.pop();
        ASSERT(!msg || dynamic_cast<cPacket*>(msg));
        return (cPacket*)msg;
    }
}


void MacBase::sendDown(cMessage *msg)
{
    EV << "sending down " << msg << "\n";

    if (msg->isPacket())
        emit(packetSentToLowerSignal, msg);

    send(msg, lowerLayerOut);
}

void MacBase::sendUp(cMessage *msg)
{
    EV << "sending up " << msg << "\n";

    if (msg->isPacket())
        emit(packetSentToUpperSignal, msg);

    send(msg, upperLayerOut);
}

