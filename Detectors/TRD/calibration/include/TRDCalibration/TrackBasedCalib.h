// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

/// \file TrackBasedCalib.h
/// \brief Provides information required for TRD calibration which is based on the global tracking
/// \author Ole Schmidt

#ifndef O2_TRD_TRACKBASEDCALIB_H
#define O2_TRD_TRACKBASEDCALIB_H

#include "DataFormatsTRD/TrackTRD.h"
#include "DataFormatsTRD/Tracklet64.h"
#include "DataFormatsTRD/CalibratedTracklet.h"
#include "DataFormatsTRD/AngularResidHistos.h"
#include "DataFormatsTRD/GainCalibHistos.h"
#include "DataFormatsTRD/NoiseCalibration.h"
#include "TRDBase/PadCalibrationsAliases.h"
#include "DetectorsBase/Propagator.h"
#include "TRDBase/RecoParam.h"

#include "Rtypes.h"

#include <gsl/span>

namespace o2
{

namespace globaltracking
{
class RecoContainer;
}

namespace trd
{

class TrackBasedCalib
{
  using MatCorrType = o2::base::Propagator::MatCorrType;

 public:
  TrackBasedCalib() = default;
  TrackBasedCalib(const TrackBasedCalib&) = delete;
  ~TrackBasedCalib() = default;

  /// Load geometry and apply magnetic field setting
  void init();

  /// Initialize the input arrays
  void setInput(const o2::globaltracking::RecoContainer& input);

  /// Set the MCM noise map
  void setNoiseMapMCM(const NoiseStatusMCM* map) { mNoiseCalib = map; };

  // Set the local gain factors with values from the ccdb
  void setLocalGainFactors(const LocalGainFactor* localGain) { mLocalGain = localGain; }

  /// Reset the output
  void reset();

  /// Main processing function for creating angular residual histograms for vDrift and ExB calibration
  void calculateAngResHistos();

  /// 3-way fit to TRD tracklets
  int doTrdOnlyTrackFits(gsl::span<const TrackTRD>& tracks);

  /// Main processing function for gathering information needed for gain calibration
  /// i.e. TRD tracklet charges vs TPC track dEdx for given momentum slice
  void calculateGainCalibObjs();

  /// Collect tracklet charges for given track
  int filldEdx(gsl::span<const TrackTRD>& tracks, bool isTPCTRD);

  /// Extrapolate track parameters to given layer and if requested perform update with tracklet
  bool propagateAndUpdate(TrackTRD& trk, int iLayer, bool doUpdate) const;

  const AngularResidHistos& getAngResHistos() const { return mAngResHistos; }
  const GainCalibHistos& getGainCalibHistos() const { return mGainCalibHistos; }

 private:
  float mMaxSnp{o2::base::Propagator::MAX_SIN_PHI};  ///< max snp when propagating tracks
  float mMaxStep{o2::base::Propagator::MAX_STEP};    ///< maximum step for propagation
  MatCorrType mMatCorr{MatCorrType::USEMatCorrNONE}; ///< if material correction should be done
  RecoParam mRecoParam;                              ///< parameters required for TRD reconstruction
  AngularResidHistos mAngResHistos;                  ///< aggregated data for the track based calibration
  GainCalibHistos mGainCalibHistos;                  ///< aggregated data for the track based calibration
  float bz;                                          ///< magnetic field

  // input arrays which should not be modified since they are provided externally
  gsl::span<const TrackTRD> mTracksInITSTPCTRD;        ///< TRD tracks reconstructed from TPC or ITS-TPC seeds
  gsl::span<const TrackTRD> mTracksInTPCTRD;           ///< TRD tracks reconstructed from TPC or TPC seeds
  gsl::span<const Tracklet64> mTrackletsRaw;           ///< array of raw tracklets needed for TRD refit
  gsl::span<const CalibratedTracklet> mTrackletsCalib; ///< array of calibrated tracklets needed for TRD refit
  gsl::span<const o2::tpc::TrackTPC> mTracksTPC;       ///< TPC tracks in order to get dEdxTPC
  gsl::span<const o2::dataformats::TrackTPCITS> mTracksITSTPC;

  // corrections from ccdb, some need to be loaded only once hence an init flag
  const LocalGainFactor* mLocalGain{nullptr}; ///< local gain factors from krypton calibration
  const NoiseStatusMCM* mNoiseCalib{nullptr}; ///< CCDB object with information of noisy MCMs

  ClassDefNV(TrackBasedCalib, 1);
};

} // namespace trd
} // namespace o2

#endif // O2_TRD_TRACKBASEDCALIB_H
