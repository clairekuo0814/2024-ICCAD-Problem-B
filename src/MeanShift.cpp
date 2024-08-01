#include "MeanShift.h"

MeanShift::MeanShift(){}

MeanShift::~MeanShift(){}

void MeanShift::run(Manager &mgr){
    DEBUG_MS("do graceful meanshift clustering...");
    DEBUG_MS("RUN RTREE")
    buildRtree(mgr);
    DEBUG_MS("RUN KNN")
    initKNN(mgr);
    DEBUG_MS("SHIFT FF")
    shiftFFs();
}

void MeanShift::buildRtree(Manager &mgr){
    rtrees.resize(mgr.TotalClk);

    // make unique id for the flipflop
    for(const auto &pair : mgr.FF_Map){
        FFs.push_back(pair.second);
    }

    for(size_t i = 0; i < FFs.size(); i++){
        FF *ff = FFs[i];
        ff->setFFIdx(i);
        rtrees[ff->getClkIdx()].insert(std::make_pair(Point(ff->getCoor().x,ff->getCoor().y), i));
    }
}

void MeanShift::initKNN(const Manager &mgr){

    #pragma omp parallel for num_threads(MAX_THREADS)
    for(size_t i = 0; i < FFs.size(); i++){
        FF *ff = FFs[i];
        ff->setNewCoor(ff->getCoor());
        FFrunKNN(mgr, i);
        
        // no need to shift
        if(ff->getNeighborSize() <= 1){
            ff->setIsShifting(false);
        }
        else{
            ff->setBandwidth(mgr);
        }
    }
}

void MeanShift::shiftFFs(){
    iterationCount.resize(FFs.size());
    for (size_t i = 0; i < FFs.size(); i++)
    {
        iterationCount[i] = {0, i};
    }
    #pragma omp parallel for num_threads(MAX_THREADS)
    for(size_t i = 0; i < FFs.size(); i++){
        FF *ff = FFs[i];
        if(!ff->getIsShifting()){
            iterationCount[i].first = 0;
            continue;
        }
        int iteration = 0;
        while(++iteration){
            double distance = ff->shift(FFs);
            if(distance < SHIFT_TOLERANCE){
                ff->setIsShifting(false);
                iterationCount[i].first = iteration;
                break;
            }
        }
    }
    double totalShift = 0;
    double maxShift = 0;
    for(size_t i = 0; i < FFs.size(); i++){
        double shift_distance = std::sqrt(SquareEuclideanDistance(FFs[i]->getCoor(), FFs[i]->getNewCoor()));
        totalShift += shift_distance;
        if(shift_distance > maxShift){maxShift = shift_distance;}
    }
    DEBUG_MS("Max shift distance: " + std::to_string(maxShift));
    DEBUG_MS("Total shift distance: " + std::to_string(totalShift));
}

void MeanShift::FFrunKNN(const Manager &mgr, int ffidx){
    FF *ff = FFs[ffidx];
    std::vector<PointWithID> neighbors;
    rtrees[ff->getClkIdx()].query(bgi::nearest(Point(ff->getCoor().x, ff->getCoor().y), 10), std::back_inserter(neighbors));
    BOOST_FOREACH(PointWithID const &p, neighbors){
        int ffneighbor_idx = p.second;
        FF *ffneighbor = FFs[ffneighbor_idx];
        double distance = SquareEuclideanDistance(ff->getCoor(), ffneighbor->getCoor());
        if(distance < mgr.param.MAX_SQUARE_DISPLACEMENT){
            ff->addNeighbor(ffneighbor_idx, distance);
        }
    }
    ff->sortNeighbors();
}