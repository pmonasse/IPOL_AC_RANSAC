/**
 * @file orsa_fundamental.cpp
 * @brief Fundamental matrix estimation with ORSA algorithm
 * @author Lionel Moisan, Pascal Monasse, Pierre Moulon
 * 
 * Copyright (c) 2011-2016 Lionel Moisan, Pascal Monasse, Pierre Moulon
 * Copyright (c) 2011-2016,2020 Pascal Monasse
 * Copyright (c) 2011-2016 Pierre Moulon
 * All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "orsa_fundamental.hpp"
#include "fundamental_model.hpp"
#include "orsa.hpp"
#include "ransac.hpp"

#include <iostream>
#include <utility>

using namespace libNumerics;

namespace orsa {

/// Display and return average/max error of inliers of fundamental matrix F.
std::pair<double,double> display_stats(const std::vector<Match>& match,
                                           const std::vector<int>& in,
                                           const matrix<double>& F) {
  std::vector<int>::const_iterator it=in.begin();
  double l2=0, linf=0;
  for(; it!=in.end(); ++it) {
    const Match& m=match[*it];
    double a = F(0,0) * m.x1 + F(1,0) * m.y1 + F(2,0);
    double b = F(0,1) * m.x1 + F(1,1) * m.y1 + F(2,1);
    double c = F(0,2) * m.x1 + F(1,2) * m.y1 + F(2,2);
    double d = a*m.x2 + b*m.y2 + c;
    double e =  (d*d) / (a*a + b*b);
    l2 += e;
    if(linf < e)
      linf = e;
  }
  std::pair<double,double> err(sqrt(l2/in.size()),sqrt(linf));
  std::cout << "Average/max error: "
            << err.first << "/" << err.second <<std::endl;
  return err;
}

static ModelEstimator* build_model(const std::vector<Match>& vec_matchings,
                                   int w1, int h1, int w2, int h2) {
  const int n = static_cast<int>( vec_matchings.size() );
  libNumerics::matrix<double> xA(2,n), xB(2,n);

  for (int i=0; i < n; ++i)
  {
    xA(0,i) = vec_matchings[i].x1;
    xA(1,i) = vec_matchings[i].y1;
    xB(0,i) = vec_matchings[i].x2;
    xB(1,i) = vec_matchings[i].y2;
  }

  return new FundamentalModel(xA, w1, h1, xB, w2, h2, true);
}

static void refine(ModelEstimator* model,
                   const std::vector<Match>& vec_matchings,
                   const std::vector<int>& vec_inliers,
                   ModelEstimator::Model *M) {
  std::cout << "Before refinement: ";
  std::pair<double,double> err = display_stats(vec_matchings, vec_inliers, *M);
  ModelEstimator::Model M2(3,3);
  if( model->ComputeModel(vec_inliers,&M2) ) // Re-estimate with all inliers
  {
    std::cout << "After  refinement: ";
    double maxBefore = err.second;
    if(display_stats(vec_matchings, vec_inliers, M2).first <= maxBefore)
      *M = M2;
    else
      std::cerr << "Warning: error after refinement is too large, thus ignored"
                <<std::endl;
  } else
    std::cerr << "Warning: error in refinement, result is suspect" <<std::endl;
}

/// Estimate the fundamental matrix using regular RANSAC and refinement.
/// \param[in] vec_matchings List of correspondences.
/// \param[in] w1,h1 Dimensions of left image.
/// \param[in] w2,h2 Dimensions of right image.
/// \param[in] precision Maximum inlier/outlier threshold (in pixels).
/// \param[in] nbIterMax Maximal number of iterations for RANSAC algorithm.
/// \param[in] beta Probability of one correct sample (to adjust iterations).
/// \param[out] H Homography registering left on right image.
/// \param[out] vec_inliers Index of inliers in \a vec_matchings.
/// \return true if at least one viable sample (ie, producing a model) is found.
bool ransac_fundamental(const std::vector<Match>& vec_matchings,
                        int w1,int h1, int w2,int h2,
                        double precision, int nbIterMax, double beta,
                        libNumerics::matrix<double>& H,
                        std::vector<int>& vec_inliers)
{
  ModelEstimator* model = build_model(vec_matchings,w1,h1,w2,h2);
  bool ok = (vec_matchings.size() >= static_cast<size_t>(model->SizeSample()));
  if(! ok)
    std::cerr << "Error: RANSAC needs " << model->SizeSample()
              << " matches or more to proceed" <<std::endl;
  else {
    Ransac ransac(model);
    nbIterMax = ransac.run(vec_inliers, precision, nbIterMax, &H, beta, true);
    std::cout << "Iterations: " << nbIterMax << std::endl;
    refine(model, vec_matchings, vec_inliers, &H);
  }
  delete model;
  return ok;
}

/// Estimate the fundamental matrix using ORSA method and refinement.
/// If the mean error after refinement exceeds the max error of ORSA result,
/// the refinement is not applied.
/// \param[in] vec_matchings List of correspondences.
/// \param[in] w1,h1 Dimensions of left image.
/// \param[in] w2,h2 Dimensions of right image.
/// \param[in,out] precision Maximum inlier/outlier threshold (in pixels).
/// \param[in] nbIter Maximal number of iterations for RANSAC algorithm.
/// \param[out] F Fundamental matrix between left and right image.
/// \param[out] vec_inliers Index of inliers in \a vec_matchings.
/// \return The status of the estimation. If no meaningful (NFA<1) model is
/// found, the \a F field is not filled and \a vec_inliers is empty.
bool orsa_fundamental(const std::vector<Match>& vec_matchings,
                      int w1,int h1, int w2,int h2,
                      double precision, int nbIter,
                      matrix<double>& F,
                      std::vector<int>& vec_inliers)
{
  ModelEstimator* model = build_model(vec_matchings,w1,h1,w2,h2);
  bool ok = (vec_matchings.size() > static_cast<size_t>(model->SizeSample()));
  if(! ok)
    std::cerr << "Error: ORSA needs " << model->SizeSample()+1
              << " matches or more to proceed" <<std::endl;
  else {
    double D, A; // Diameter and area of image
    D = sqrt(w1*(double)w1 + h1*(double)h1);
    A = w1*(double)h1;
    double alpha0Left  = 2.0*D/A;
    D = sqrt(w2*(double)w2 + h2*(double)h2);
    A = w2*(double)h2;
    double alpha0Right = 2.0*D/A;
    Orsa orsa(model, alpha0Left, alpha0Right);
    if(orsa.run(vec_inliers, nbIter, &precision, &F, true)>0.0)
      ok = false;
    else
      refine(model, vec_matchings, vec_inliers, &F);
  }
  delete model;
  return true;
}

} // namespace orsa
