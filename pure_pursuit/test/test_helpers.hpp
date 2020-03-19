/*
 * test_helpers.hpp
 *
 *  Created on: Mar 19, 2020
 *      Author: jelavice
 */

#pragma once

#include "pure_pursuit/math.hpp"

#include <random>



namespace pure_pursuit_test {

using Circle  = pure_pursuit::Circle;
using Line = pure_pursuit::Line;
using Vector = pure_pursuit::Vector;
using Matrix = pure_pursuit::Matrix;
using Point = pure_pursuit::Point;

extern std::mt19937 rndGenerator;
constexpr double testPlaneWidth = 100.0;

int seedRndGenerator();
Circle createRandomCircle();
Vector createUnitVectorPerpendicularToLine(const Line &line);
Line createRandomLineWithoutIntersection(const Circle &circle);
Line createRandomLineWitOneIntersection(const Circle &circle);
Line createRandomLineWithTwoIntersections(const Circle &circle);
Point createRandomPoint();
Point createRandomPointOutside( const Circle &circle);
Point createRandomPointInside( const Circle &circle);

} /* namespace pure_pursuit_test */