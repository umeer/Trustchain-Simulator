#ifdef _MSC_VER
#pragma warning(disable:4786)
#endif

#include "Routing.h"

using namespace omnetpp;

Define_Module(Routing);

void Routing::initialize() {
    myAddress = getParentModule()->par("address");

    dropSignal = registerSignal("drop");
    outputIfSignal = registerSignal("outputIf");

    //
    // Brute force approach -- every node does topology discovery on its own,
    // and finds routes to all other nodes independently, at the beginning
    // of the simulation. This could be improved: (1) central routing database,
    // (2) on-demand route calculation
    //
    cTopology *topo = new cTopology("topo");

    std::vector<std::string> nedTypes;
    nedTypes.push_back(getParentModule()->getNedTypeName());
    topo->extractByNedTypeName(nedTypes);
    EV << "cTopology found " << topo->getNumNodes() << " nodes\n";

    cTopology::Node *thisNode = topo->getNodeFor(getParentModule());

    // find and store next hops
    for (int i = 0; i < topo->getNumNodes(); i++) {
        if (topo->getNode(i) == thisNode)
            continue;  // skip ourselves
        topo->calculateUnweightedSingleShortestPathsTo(topo->getNode(i));

        if (thisNode->getNumPaths() == 0)
            continue;  // not connected

        cGate *parentModuleGate = thisNode->getPath(0)->getLocalGate();
        int gateIndex = parentModuleGate->getIndex();
        int address = topo->getNode(i)->getModule()->par("address");
        rtable[address] = gateIndex;
        EV << "  towards address " << address << " gateIndex is " << gateIndex
                  << endl;

        if (thisNode->getDistanceToTarget() == 1) {
            neighbourNodeAddresses.push_back(i);
        }
    }

    delete topo;
}

void Routing::handleMessage(cMessage *msg) {
    Packet *pk = check_and_cast<Packet *>(msg);

    if (pk->getDestAddr() == myAddress) {
        EV << "local delivery of packet " << pk->getName() << endl;
        send(pk, "localOut");
        emit(outputIfSignal, -1);  // -1: local
        return;
    } else {
        sendOut(pk);
    }
}

void Routing::sendOut(Packet *pk) {
    RoutingTable::iterator it = rtable.find(pk->getDestAddr());
    if (it == rtable.end()) {
        EV << "address " << pk->getDestAddr()
                  << " unreachable, discarding packet " << pk->getName()
                  << endl;
        emit(dropSignal, (long) pk->getByteLength());
        delete pk;
        return;
    }

    int outGateIndex = (*it).second;
    EV << "forwarding packet " << pk->getName() << " on gate index "
              << outGateIndex << endl;
    pk->setHopCount(pk->getHopCount() + 1);
    emit(outputIfSignal, outGateIndex);

    send(pk, "out", outGateIndex);
}
