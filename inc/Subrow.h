#ifndef _SUBROW_H_
#define _SUBROW_H_

// Subrow class used in legalizer code
#include <iostream>
#include <set>
#include "Cell.h"
#include "Row.h"


class Row;
class Subrow{
private:
    double startX;
    double endX;
    double freeWidth;
    double height;
    std::set<Cell *> reject_cells;
    int rowIdx;

public:
    Subrow();
    ~Subrow();

    // Setters
    void setStartX(double startX);
    void setEndX(double endX);
    void setFreeWidth(double freeWidth);
    void setHeight(double height);
    void addRejectCell(Cell *cell);
    void setRowIdx(int rowIdx);

    // Getters
    double getStartX()const;
    double getEndX()const;
    double getFreeWidth()const;
    double getHeight()const;
    bool hasCell(Cell *cell);
    int getRowIdx()const;

    friend std::ostream &operator<<(std::ostream &os, const Subrow &subrow);
};

#endif