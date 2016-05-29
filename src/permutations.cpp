#include <thread>

#include "utils.hpp"
#include "netStats.hpp"
#include "progress.hpp"

#define ARMA_NO_DEBUG

/* Generate null-distribution observations for the module preservation statistics
 * 
 * Fills out the corresponding slices of the provided 'nulls' cube based on the
 * number of permutations requested, and the start index.
 * 
 * @param tDataScaled pointer to the (scaled) test data matrix.
 * @param tCorrPtr pointer to the test correlation matrix.
 * @param tNetPtr pointer to the test network matrix.
 * @param discWD unordered_map containing the weighted degree vectors for all
 *   modules in the discovery dataset.
 * @param discNC unordered_map containing the node contribution vectors for all
 *   modules in the discovery dataset.
 * @param discCV unordered_map containing the correlation coefficient vectors
 *   for all modules in the discovery dataset.
 * @param mods vector of modules for which the module preservation statistics
 *   are being calculated for.
 * @param modNodeMap mapping of module labels to node IDs.
 * @param modIdxMap mapping of module labels to 'nulls' cube row indices.
 * @param nullIdx a vector of node IDs to be shuffled in the permutation 
 *  procedure.
 * @param nullMap mapping of node IDs to indices in 'nullIdx'.
 * @param nulls cube to store the results in
 * @param nPerm number of permutations to calculate.
 * @param start slice index to start at when filling in the 'nulls' cube.
 * @param progress vector to fill in number of permutations completed for this
 *   thread.
 * @param thread the number of the thread.
 * @param interrupted variable on the heap checking whether the user has asked
 *   to cancel the computation
 */
void calculateNulls(
  const arma::mat& tDataScaled, const arma::mat& tCorrPtr, 
  const arma::mat& tNetPtr, vecmap& discWD, vecmap& discNC, 
  vecmap& discCV, const std::vector<std::string> mods, 
  const stringmap modNodeMap, const namemap modIdxMap, arma::uvec nullIdx, 
  namemap nullMap, arma::cube& nulls, unsigned int nPerm, unsigned int start, 
  arma::uvec& progress, unsigned int thread, bool& interrupted
) {    
  /**
   * The R API is single threaded, we *must not* access it
   * at all during our parallel calls.
   **/
  unsigned int modIdx;
  std::string mod;
  arma::uvec tIdx, tRank;
  arma::vec tWD, tSP, tNC, tCV;
  for (unsigned int pp = start; pp < start + nPerm; ++pp) {
    nullIdx = arma::shuffle(nullIdx);
    for (auto mi = mods.begin(); mi != mods.end(); ++mi) {
      if (interrupted) return; 
      // What module are we analysing, and what index does it have internally?
      mod = *mi; 
      modIdx = modIdxMap.at(mod);
     
      // Get the node indices in the test dataset for this module
      tIdx = GetRandomIdx(mod, modNodeMap, nullIdx, nullMap);
      
      // Now calculate required properties in the test dataset
      tCV = CorrVector(tCorrPtr, tIdx);
      if (interrupted) return; 
     
      tRank = sortNodes(tIdx); // Sort nodes for sequential memory access
      tWD = WeightedDegree(tNetPtr, tIdx)(tRank);
      if (interrupted) return; 
      tSP = SummaryProfile(tDataScaled, tIdx);
      if (interrupted) return; 
      tNC = NodeContribution(tDataScaled, tIdx, tSP)(tRank);
      if (interrupted) return; 
     
      // Calculate and store test statistics in the appropriate location in the 
      // results matrix
      
      nulls.at(modIdx, 0, pp) = AverageEdgeWeight(tWD);
      nulls.at(modIdx, 1, pp) = ModuleCoherence(tNC);
      nulls.at(modIdx, 2, pp) = Correlation(discCV[mod], tCV);
      nulls.at(modIdx, 3, pp) = Correlation(discWD[mod], tWD);
      nulls.at(modIdx, 4, pp) = Correlation(discNC[mod], tNC);
      nulls.at(modIdx, 5, pp) = SignAwareMean(discCV[mod], tCV);
      nulls.at(modIdx, 6, pp) = SignAwareMean(discNC[mod], tNC);
    }
    progress[thread]++; 
  }
}

//' Multithreaded permutation procedure for module preservation statistics
//' 
//' @details
//' \subsection{Input expectations:}{
//'   Note that this function expects all inputs to be sensible, as checked by
//'   the R function 'checkUserInput' and processed by 'modulePreservation'. 
//'   
//'   These requirements are:
//'   \itemize{
//'   \item{The ordering of node names across 'dData', 'dCorr', and 'dNet' is
//'         consistent.}
//'   \item{The ordering of node names across 'tData', 'tCorr', and 'tNet' is
//'         consistent.}
//'   \item{The columns of 'dData' and 'tData' are the nodes.}
//'   \item{'dCorr', 'dNet', 'tCorr', and 'tNet' are square matrices, and their
//'         rownames are identical to their column names.}
//'   \item{'moduleAssigments' is a named character vector, where the names
//'         represent node labels found in the test dataset (i.e. 'tNet').
//'         The vector must not include nodes that are not present in the 
//'         test dataset.}
//'   \item{The labels in 'modules' must all be present in 'moduleAssignments'.}
//'   \item{'nPermutations' is a single number, greater than 0.}
//'   \item{'nCores' is a single number, greater than 0. Note, this number must
//'         not be larger than the number of cores on your machine, or the 
//'         number of cores allocated to your job!}
//'   \item{'nullHypothesis' must be a character vector of length 1, containing
//'         either "overlap" or "all".}
//'   \item{'verbose' must be a logical vector of length 1 containing either 
//'         'TRUE' or 'FALSE'.}
//'   \item{'vCat' must be the function NetRep:::vCat.}
//'   }
//' }
//' 
//' @param dData data matrix from the \emph{discovery} dataset.
//' @param dCorr matrix of correlation coefficients between all pairs of 
//'   variables/nodes in the \emph{discovery} dataset.
//' @param dNet adjacency matrix of network edge weights between all pairs of 
//'   nodes in the \emph{discovery} dataset.
//' @param tData data matrix from the \emph{test} dataset.
//' @param tCorr matrix of correlation coefficients between all pairs of 
//'   variables/nodes in the \emph{test} dataset.
//' @param tNet adjacency matrix of network edge weights between all pairs of 
//'   nodes in the \emph{test} dataset.
//' @param moduleAssignments a named character vector containing the module 
//'   each node belongs to in the discovery dataset. 
//' @param modules a character vector of modules for which to calculate the 
//'   module preservation statistics.
//' @param nPermutations the number of permutations from which to generate the
//'   null distributions for each statistic.
//' @param nCores the number of cores that the permutation procedure may use.
//' @param nullHypothesis either "overlap" or "all".
//' @param verbose if 'true', then progress messages are printed.
//' @param vCat the vCat function must be passed in so that it can be called 
//'  for output logging. 
//' 
//' @return a list containing a matrix of observed test statistics, and an
//'   array of null distribution observations.
// [[Rcpp::export]]
Rcpp::List PermutationProcedure (
  Rcpp::NumericMatrix dData, Rcpp::NumericMatrix dCorr, Rcpp::NumericMatrix dNet,
  Rcpp::NumericMatrix tData, Rcpp::NumericMatrix tCorr, Rcpp::NumericMatrix tNet,
  Rcpp::CharacterVector moduleAssignments, Rcpp::CharacterVector modules,
  Rcpp::IntegerVector nPermutations, Rcpp::IntegerVector nCores, 
  Rcpp::CharacterVector nullHypothesis, Rcpp::LogicalVector verbose,
  Rcpp::Function vCat
) {
  // First, we need to create pointers to the memory holding each
  // NumericMatrix that can be recognised by the armadillo library.
  const arma::mat& dDataPtr = arma::mat(dData.begin(), dData.nrow(), dData.ncol(), false, true);
  const arma::mat& dCorrPtr = arma::mat(dCorr.begin(), dCorr.nrow(), dCorr.ncol(), false, true);
  const arma::mat& dNetPtr = arma::mat(dNet.begin(), dNet.nrow(), dNet.ncol(), false, true);
  const arma::mat& tDataPtr = arma::mat(tData.begin(), tData.nrow(), tData.ncol(), false, true);
  const arma::mat& tCorrPtr = arma::mat(tCorr.begin(), tCorr.nrow(), tCorr.ncol(), false, true);
  const arma::mat& tNetPtr = arma::mat(tNet.begin(), tNet.nrow(), tNet.ncol(), false, true);
  
  // Next we will scale the matrix data
  const arma::mat dDataScaled = Scale(dDataPtr);
  const arma::mat tDataScaled = Scale(tDataPtr);
  
  R_CheckUserInterrupt(); 
  
  // convert the colnames / rownames to C++ equivalents
  const std::vector<std::string> dNames (Rcpp::as<std::vector<std::string>>(colnames(dNet)));
  const std::vector<std::string> tNames (Rcpp::as<std::vector<std::string>>(colnames(tNet)));
  
  /* Next, we need to create three mappings:
   *  - From node IDs to indices in the discovery dataset
   *  - From node IDs to indices in the test dataset
   *  - From modules to node IDs
   */
  const namemap dIdxMap = MakeIdxMap(dNames);
  const namemap tIdxMap = MakeIdxMap(tNames);
  const stringmap modNodeMap = MakeModMap(moduleAssignments);

  // What modules do we actually want to analyse?
  const std::vector<std::string> mods (Rcpp::as<std::vector<std::string>>(modules));
  
  // Map module labels to row indices in our armadillo matrices/cubes
  const namemap modIdxMap = MakeIdxMap(mods);
  
  // Typecast function options from R's vectors to appropriate C++ scalar 
  // equivalents
  std::string nullType = Rcpp::as<std::string>(nullHypothesis[0]);
  unsigned int nThreads = nCores[0];
  unsigned int nPerm = nPermutations[0];
  const bool verboseFlag = verbose[0];
  
  // Initialise results containers
  arma::mat obs (mods.size(), 7); // stores the observed test statistics
  arma::cube nulls (mods.size(), 7, nPerm); // stores the null distributions
  nulls.fill(NA_REAL); // Fill with NAs so killed results make sense
  
  // We will save the weighted degree, node contribution, and correlation
  // vectors in the observed dataset so we don't have to compute these at
  // every permutation.
  vecmap discWD, discNC, discCV;
  
  R_CheckUserInterrupt(); 
  
  /* For the permutation procedure, we need to shuffle a vector of *valid*
   * indices in the test network: if the null hypothesis is "overlap" (the
   * default) then only nodes that are present in both the discovery and test
   * datasets are used to generate the null distributions.
   * 
   * So we need:
   *  - A *vector* of indices in the test dataset that can be shuffled
   *  - A mapping from the valid node IDs to their indices in the vector to 
   *    be shuffled
   */
  arma::uvec nullIdx;
  namemap nullMap;
  if (nullType == "overlap") {
    // The valid nodes are the names of the moduleAssignments vector
    nullIdx.set_size(moduleAssignments.length());
    nullMap = MakeNullMap(moduleAssignments.names(), tIdxMap, nullIdx);
  } else { // otherwise take all nodes
    nullIdx.set_size(tNetPtr.n_cols);
    nullMap = MakeNullMap(tNames, tIdxMap, nullIdx);
  }
  
  // Calculate some network properties in the discovery dataset.
  R_CheckUserInterrupt(); 
  std::string mod;
  arma::uvec dIdx, dRank;
  arma::vec dSP; 
  for (auto mi = mods.begin(); mi != mods.end(); ++mi) {
    // Get the node indices in the discovery dataset for this module
    mod = *mi;
    dIdx = GetNodeIdx(mod, modNodeMap, dIdxMap);
    R_CheckUserInterrupt(); 

    // Calculate the network properties and insert into their storage containers
    discCV[mod] = CorrVector(dCorrPtr, dIdx);
    R_CheckUserInterrupt(); 

    dRank = sortNodes(dIdx); // Sort nodes for sequential memory access
    discWD[mod] = WeightedDegree(dNetPtr, dIdx)(dRank);
    R_CheckUserInterrupt(); 
    dSP = SummaryProfile(dDataScaled, dIdx);
    R_CheckUserInterrupt(); 
    discNC[mod] = NodeContribution(dDataScaled, dIdx, dSP)(dRank);
    R_CheckUserInterrupt(); 
  }

  // Now calculate the observed test statistics
  vCat(verbose, 1, "Calculating observed test statistics...");
  unsigned int modIdx;
  arma::uvec tIdx, tRank;
  arma::vec tCV, tWD, tSP, tNC;
  for (auto mi = mods.begin(); mi != mods.end(); ++mi) {
    // What module are we analysing, and what index does it have internally?
    mod = *mi;
    modIdx = modIdxMap.at(mod);
    
    // Get the node indices in the test dataset for this module
    tIdx = GetNodeIdx(mod, modNodeMap, tIdxMap);

    // Now calculate required properties in the test dataset
    tCV = CorrVector(tCorrPtr, tIdx);
    
    tRank = sortNodes(tIdx); // Sort nodes for sequential memory access
    tWD = WeightedDegree(tNetPtr, tIdx)(tRank);
    tSP = SummaryProfile(tDataScaled, tIdx);
    tNC = NodeContribution(tDataScaled, tIdx, tSP)(tRank);

    /* Calculate and store test statistics in the appropriate location in the 
     * results matrix
     */
    obs(modIdx, 0) = AverageEdgeWeight(tWD);
    obs(modIdx, 1) = ModuleCoherence(tNC);
    obs(modIdx, 2) = Correlation(discCV[mod], tCV);
    obs(modIdx, 3) = Correlation(discWD[mod], tWD);
    obs(modIdx, 4) = Correlation(discNC[mod], tNC);
    obs(modIdx, 5) = SignAwareMean(discCV[mod], tCV);
    obs(modIdx, 6) = SignAwareMean(discNC[mod], tNC);
  }
  
  if (nThreads == 1) {
    vCat(verbose, 1, "Generating null distributions from", nPerm, 
         "permutations using", nThreads, "thread...");
  } else {
    vCat(verbose, 1, "Generating null distributions from", nPerm, 
         "permutations using", nThreads, "threads...");
  }

  // Create nThreads to run the permutation procedure in parallel
  std::thread *tt = new std::thread[nThreads];
  
  // Determine the number of permutations for each thread
  arma::uvec chunkPerms (nThreads, arma::fill::zeros);
  for (unsigned int ii = 0; ii < nThreads; ++ii) {
    chunkPerms.at(ii) = nPerm / nThreads; 
  }
  // When the number of permtutations cannot be distributed evenly, spread the
  // remainder out across threads.
  for (unsigned int ii = 0; ii < nPerm % nThreads; ++ii) {
    chunkPerms.at(ii)++;
  }
  
  // Set up the slice indices each thread should start at in the results cube
  arma::uvec startIdx (nThreads, arma::fill::zeros);
  for (unsigned int ii = 0; ii < nThreads; ++ii) {
    for (unsigned int jj = 0; jj < ii; ++jj) {
      startIdx.at(ii) += chunkPerms.at(jj);
    }
  }
  
  // Set up the progress bar
  arma::uvec progress (nThreads, arma::fill::zeros);
  
  // This variable stored on the heap will get modified to be 'true' of ^C is
  // entered in the R terminal (see 'MonitorProgress'). Each thread will check
  // this, then abort if it is 'true'
  bool interrupted = false;
  
  // Spawn the threads
  for (unsigned int ii = 0; ii < nThreads; ++ii) {
    tt[ii] = std::thread(
      calculateNulls, std::ref(tDataScaled), std::ref(tCorrPtr), 
      std::ref(tNetPtr), std::ref(discWD), std::ref(discNC), std::ref(discCV),
      mods, modNodeMap, modIdxMap, nullIdx, nullMap, std::ref(nulls), 
      chunkPerms.at(ii), startIdx.at(ii), std::ref(progress), ii, 
      std::ref(interrupted)
    );
  }
  
  MonitorProgress(nPerm, progress, interrupted, verboseFlag);

  // Wait for all the threads to finish
  for (unsigned int ii = 0; ii < nThreads; ++ii) {
    tt[ii].join();
  }
  
  // Convert any NaNs to NA_REALs
  for (auto it = obs.begin(); it < obs.end(); ++it) {
    if (isnan(*it)) {
      *it = NA_REAL;
    }
  }
  
  for (auto it = nulls.begin(); it < nulls.end(); ++it) {
    if (isnan(*it)) {
      *it = NA_REAL;
    }
  }
  
  // Construct rownames
  const std::vector<std::string> statnames = {
    "avg.weight", "coherence", "cor.cor", "cor.degree", "cor.contrib", 
    "avg.cor", "avg.contrib"
  };
  
  // Construct permutation names
  std::vector<std::string> permNames(nPerm);
  for (unsigned int ii = 0; ii < permNames.size(); ++ii) {
    permNames[ii] = "permutation." + std::to_string(ii + 1);
  }
  
  // Convert cube of null distribution objects into an R array before returning
  Rcpp::NumericVector nullsArray (nulls.begin(), nulls.end());
  nullsArray.attr("dim") = Rcpp::IntegerVector::create(mods.size(), 7, nPerm);
  nullsArray.attr("dimnames") = Rcpp::List::create(
    modules, Rcpp::CharacterVector(statnames.begin(), statnames.end()), 
    permNames);
  
  // Convert matrix of observed test statistics into an R object before
  // returning
  Rcpp::NumericMatrix observed (obs.n_rows,  obs.n_cols, obs.begin());
  colnames(observed) = Rcpp::CharacterVector(statnames.begin(), statnames.end());
  rownames(observed) = modules;
  
  return Rcpp::List::create(
    Rcpp::Named("nulls") = nullsArray,
    Rcpp::Named("observed") = observed
  );
}