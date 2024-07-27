#include "FastLegalizer.h"

Fast_Legalizer::Fast_Legalizer(Manager& mgr) : mgr(mgr){
    timer.start();
}

Fast_Legalizer::~Fast_Legalizer(){
    for(auto &ff : ffs){
        delete ff;
    }
    for(auto &gate : gates){
        delete gate;
    }
    for(auto &row : rows){
        delete row;
    }
}

void Fast_Legalizer::run(){
    LoadFF();
    LoadGate();
    LoadPlacementRow();
    SliceRowsByRows();
    SliceRowsByGate();
    Tetris();
    LegalizeWriteBack();
    timer.stop();
}

void Fast_Legalizer::LoadFF(){
    DEBUG_FASTLGZ("Load FF to Databse");
    for(const auto &pair : mgr.FF_Map){
        Node *ff = new Node();
        ff->setName(pair.second->getInstanceName());
        ff->setGPCoor(pair.second->getNewCoor());
        ff->setLGCoor(Coor(DBL_MAX, DBL_MAX));
        ff->setCell(pair.second->getCell());
        ff->setW(pair.second->getW());
        ff->setH(pair.second->getH());
        ff->setIsPlace(false);
        ff->setTNS(pair.second->getTNS());
        ffs.emplace_back(ff);
    }
}

void Fast_Legalizer::LoadGate(){
    DEBUG_FASTLGZ("Load Gate to Databse");
    for(const auto &pair: mgr.Gate_Map){
        Node *gate = new Node();
        gate->setName(pair.second->getInstanceName());
        gate->setGPCoor(pair.second->getCoor());
        gate->setLGCoor(pair.second->getCoor());
        gate->setCell(nullptr);
        gate->setW(pair.second->getW());
        gate->setH(pair.second->getH());
        gate->setIsPlace(false);
        gates.emplace_back(gate);
    }
}

void Fast_Legalizer::LoadPlacementRow(){
    DEBUG_FASTLGZ("Load Placement Row to Databse");
    std::vector<PlacementRow> PlacementRows = mgr.die.getPlacementRows();
    for(size_t i = 0; i < PlacementRows.size(); i++){
        Row *row = new Row();
        row->setStartCoor(PlacementRows[i].startCoor);
        row->setSiteHeight(mgr.die.getDieBorder().y - PlacementRows[i].startCoor.y);
        row->setSiteWidth(PlacementRows[i].siteWidth);
        row->setNumOfSite(PlacementRows[i].NumOfSites);
        row->setEndX(PlacementRows[i].startCoor.x + PlacementRows[i].siteWidth * PlacementRows[i].NumOfSites);

        // init Row::subrows
        Subrow *subrow = new Subrow();
        subrow->setStartX(PlacementRows[i].startCoor.x);
        subrow->setEndX(PlacementRows[i].startCoor.x + PlacementRows[i].siteWidth * PlacementRows[i].NumOfSites);
        subrow->setFreeWidth(PlacementRows[i].siteWidth * PlacementRows[i].NumOfSites);
        subrow->setHeight(mgr.die.getDieBorder().y - PlacementRows[i].startCoor.y);
        row->addSubrows(subrow);
        rows.emplace_back(row);
    }

    // sort row by the y coordinate in ascending order, if tie, sort by x in ascending order
    std::sort(rows.begin(), rows.end(), [](const Row *a, const Row *b){
        return *a < *b;
    });
}


void Fast_Legalizer::SliceRowsByRows(){
    for(size_t i = 0; i < rows.size() - 1; i++){
        double startX = rows[i]->getStartCoor().x;
        double endX = rows[i]->getEndX();
        double siteHeight = rows[i]->getSiteHeight();
        std::list<XTour> xList;
        for(size_t j = i + 1; j < rows.size(); j++){
            if(rows[j]->getEndX() <= startX || rows[j]->getStartCoor().x >= endX) continue;
            Node * upRow = new Node();
            upRow->setGPCoor(rows[j]->getStartCoor());
            upRow->setLGCoor(rows[j]->getStartCoor());
            upRow->setW(rows[j]->getEndX() - rows[j]->getStartCoor().x);
            upRow->setH(rows[j]->getSiteHeight());
            rows[i]->slicing(upRow);
            UpdateXList(rows[j]->getStartCoor().x, rows[j]->getEndX(), xList);

            if((*xList.begin()).startX <= startX && (*xList.begin()).endX >= endX){
                siteHeight = rows[j]->getStartCoor().y - rows[i]->getStartCoor().y;
                break;
            }
        }
        rows[i]->setSiteHeight(siteHeight);
    }
}

void Fast_Legalizer::SliceRowsByGate(){
    DEBUG_FASTLGZ("Seperate PlacementRows by Gate Cell");
    for(const auto &gate : gates){
        for(auto &row : rows){
            if(row->getStartCoor().y > gate->getGPCoor().y + gate->getH()) break;
            row->slicing(gate);
        }
        gate->setIsPlace(true);
    }
}
 

void Fast_Legalizer::Tetris(){
    DEBUG_FASTLGZ("Start Legalize FF");

    // Stage 1: place cells into their nearest rows (legalize y coordinate)
    for(size_t i = 0; i < ffs.size(); i++){
        size_t row_idx = FindClosestRow(ffs[i]);
        ffs[i]->setLGCoor(Coor(ffs[i]->getGPCoor().x, rows[row_idx]->getStartCoor().y));
        ffs[i]->setIsPlace(false);
        ffs[i]->setClosestRowIdx(row_idx);
    }

    // Stage 2: sort all cells acording to the sizes from the largest to smallest
    std::sort(ffs.begin(), ffs.end(), [](const Node *a, const Node *b){
        double costA = a->getH() * a->getW();
        double costB = b->getH() * b->getW();
        if(costA != costB)
            return costA > costB;
        else
            return a->getTNS() > b->getTNS();
    });

    // Stage 3: assign the x coordinate for all cells acording to the sorting order
    int place = 0;
    int not_place = 0;
    for(const auto &ff : ffs){
        Coor coor;
        size_t closest_row_idx = ff->getClosestRowIdx();
        double minDisplacement = PlaceFF(ff, closest_row_idx, coor);

        if(ff->getIsPlace()){
            place++;
            for(auto &row : rows){
                if(row->getStartCoor().y > ff->getLGCoor().y + ff->getH()) break;
                row->slicing(ff);
            }
        }
        else{
            rows[closest_row_idx]->addRejectCell(ff->getCell());
            fail_ffs.push_back(ff);
            not_place++;
        }
    }
    std::cout << "place: " << place << ", " << not_place << std::endl;

    // Stage 4: assgin not place ff x coordinate
    int stride = 1;
    while(fail_ffs.size() != 0){
        int place = 0;
        int not_place = 0;
        for(auto it = fail_ffs.begin(); it != fail_ffs.end();){
            Node *ff = (*it);
            int up_row_idx = ff->getClosestRowIdx() + stride;
            int down_row_idx = ff->getClosestRowIdx() - stride;
            double upDisplacement = DBL_MAX;
            double downDisplacement = DBL_MAX;
            Coor upBestCoor;
            Coor downBestCoor;
            if(up_row_idx < (int)rows.size()){
                if(!rows[up_row_idx]->hasCell(ff->getCell()))
                    upDisplacement = PlaceFF(ff, up_row_idx, upBestCoor);
            }
            if(down_row_idx >= 0){
                if(!rows[down_row_idx]->hasCell(ff->getCell()))
                    downDisplacement = PlaceFF(ff, down_row_idx, downBestCoor);
            }

            Coor bestCoor = upDisplacement > downDisplacement ? downBestCoor : upBestCoor;
            ff->setLGCoor(bestCoor);

            if(ff->getIsPlace()){
                place++;
                for(auto &row : rows){
                    if(row->getStartCoor().y > ff->getLGCoor().y + ff->getH()) break;
                    row->slicing(ff);
                }
                it = fail_ffs.erase(it);
            }
            else{
                not_place++;
                rows[up_row_idx]->addRejectCell(ff->getCell());
                rows[down_row_idx]->addRejectCell(ff->getCell());
                ++it;
            }
        }
        std::cout << "stride: " << stride << " " << "place: " << place << ", " << not_place << std::endl;
        stride++;
    }

}

void Fast_Legalizer::LegalizeWriteBack(){
    DEBUG_FASTLGZ("Write Back Legalize Coordinate");
    for(const auto &ff : ffs){
        if(ff->getIsPlace()){
            mgr.FF_Map[ff->getName()]->setNewCoor(ff->getLGCoor());
            //std::cout << *ff << std::endl;
        }
        else{
            mgr.FF_Map[ff->getName()]->setNewCoor(Coor(0, 0));
        }
    }
}

// Helper Function
void Fast_Legalizer::UpdateXList(double start, double end, std::list<XTour> & xList){
    if(xList.empty()){
        XTour insertRow;
        insertRow.startX = start;
        insertRow.endX = end;
        xList.push_back(insertRow);
        return;
    }

    auto iter1 = xList.begin();
    bool startIsFound = false;
    while(iter1 != xList.end() && start >= (*iter1).startX){
        if(start >= (*iter1).startX && start <= (*iter1).endX){
            startIsFound = true;
            break;
        } 
        iter1++;
    }
    auto iter2 = iter1;
    bool endIsFound = false;
    while(iter2 != xList.end() && end >= (*iter2).startX){
        if(end >= (*iter2).startX && end <= (*iter2).endX){
            endIsFound = true;
            break;
        }
        iter2++;
    }
    if(!startIsFound && !endIsFound){
        std::cout << "All Not Found!" << std::endl;
        XTour insertRow = {start, end};
        if(iter1 != iter2){
            auto iter = xList.erase(iter1, iter2);
            xList.insert(iter, insertRow);
        }else{
            xList.insert(iter1, insertRow);
        }
    }else{
        if(iter1 == iter2){
            (*iter1).endX = ((*iter1).endX >= end) ? (*iter1).endX : end;
        }else{
            XTour insertRow = {start, end};
            std::list<XTour>::iterator eraseEnd;
            if(endIsFound){
                eraseEnd = next(iter2);
            }else{
                eraseEnd = iter2;
            }
            auto iter = xList.erase(iter1, eraseEnd);
            xList.insert(iter, insertRow);
        }
    }
}

size_t Fast_Legalizer::FindClosestRow(Node *ff) {
    double targetY = ff->getGPCoor().y;
    size_t left = 0;
    size_t right = rows.size() - 1;

    while (left < right) {
        size_t mid = left + (right - left) / 2;
        double midY = rows[mid]->getStartCoor().y;
        
        if (midY == targetY) {
            return mid;
        } else if (midY < targetY) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }

    // Check the nearest row among the final candidates
    if (left > 0 && std::abs(targetY - rows[left]->getStartCoor().y) >= std::abs(targetY - rows[left - 1]->getStartCoor().y)) {
        return left - 1;
    }

    return left;
}


int Fast_Legalizer::FindClosestSubrow(Node *ff, Row *row){
    const auto &subrows = row->getSubrows();
    assert(subrows.size() > 0);

    double startX = ff->getGPCoor().x;
    for(int i = 0; i < (int)subrows.size(); i++){

        if(subrows[i]->getStartX() > startX){
            return (i - 1) >= 0 ? (i - 1): 0;
        }
    }
    return subrows.size() - 1;
}

double Fast_Legalizer::PlaceFF(Node *ff, size_t row_idx, Coor &bestCoor){
    double minDisplacement = ff->getDisplacement(Coor(DBL_MAX, DBL_MAX));
    const auto &subrows = rows[row_idx]->getSubrows();
    for(size_t i = 0; i < subrows.size(); i++){
        const auto &subrow = subrows[i];
        double alignedStartX = rows[row_idx]->getStartCoor().x + std::ceil((int)(subrow->getStartX() - rows[row_idx]->getStartCoor().x) / rows[row_idx]->getSiteWidth()) * rows[row_idx]->getSiteWidth();
        for(int x = alignedStartX; x <= subrow->getEndX(); x += rows[row_idx]->getSiteWidth()){
            Coor currCoor = Coor(x, rows[row_idx]->getStartCoor().y);
            if(ff->getDisplacement(currCoor) > minDisplacement){
                Coor subrowEndCoor = Coor(subrow->getEndX(), rows[row_idx]->getStartCoor().y);
                // If current subrow can't find better solution
                if(ff->getDisplacement(subrowEndCoor) > minDisplacement) break;
                continue;
            } 
            bool placeable = ContinousAndEmpty(x, rows[row_idx]->getStartCoor().y, ff->getW(), ff->getH(), row_idx);
            double displacement = ff->getDisplacement(currCoor);
            if(placeable && displacement < minDisplacement){
                minDisplacement = displacement;
                ff->setLGCoor(currCoor);
                bestCoor = currCoor;
                ff->setIsPlace(true);
            }
        }
    }
    return minDisplacement;
}

    

bool Fast_Legalizer::ContinousAndEmpty(double startX, double startY, double w, double h, int row_idx){
    double endY = startY + h;
    double currentY = startY;

    for(size_t i = row_idx; i < rows.size(); i++){
        double rowStartY = rows[i]->getStartCoor().y;
        double rowStartX = rows[i]->getStartCoor().x;
        double rowEndX = rowStartX + rows[i]->getSiteWidth() * rows[i]->getNumOfSite();

        // check if the current row is upper than currentY -> early break
        if(rowStartY > currentY) break;
        // check if the current row is target row -> jump condition
        if(rowStartY != currentY || startX < rows[i]->getStartCoor().x || startX + w > rowEndX) continue;
        // Save highest space can place in range startX to startX + w
        double placeH = DBL_MAX;
        // check if this row is continuous from startX to startX + w
        if(rows[i]->canPlace(startX, startX + w, placeH)){
            // update current continuous y
            currentY += placeH;

            // Check if we reached the target height
            if(currentY >= endY) return true;
        }
    }
    return false;
}