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

/// \author R+Preghenella - June 2017

#include "Generators/PrimaryGenerator.h"
#include <Generators/PrimaryGeneratorParam.h>
#include "Generators/Generator.h"
#include "SimConfig/InteractionDiamondParam.h"
#include "SimulationDataFormat/MCEventHeader.h"
#include "SimulationDataFormat/MCGenProperties.h"
#include "DataFormatsCalibration/MeanVertexObject.h"
#include "DetectorsBase/Stack.h"
#include <fairlogger/Logger.h>

#include "FairGenericStack.h"
#include "TFile.h"
#include "TTree.h"

#include "TDatabasePDG.h"
#include "TVirtualMC.h"

using o2::dataformats::MCEventHeader;

namespace o2
{
namespace eventgen
{

/*****************************************************************/

PrimaryGenerator::~PrimaryGenerator()
{
  /** destructor **/

  if (mEmbedFile && mEmbedFile->IsOpen()) {
    mEmbedFile->Close();
    delete mEmbedFile;
  }
  if (mEmbedEvent) {
    delete mEmbedEvent;
  }
}

/*****************************************************************/

Bool_t PrimaryGenerator::Init()
{
  /** init **/

  LOG(info) << "Initialising primary generator";

  // set generator ID and description
  auto& params = PrimaryGeneratorParam::Instance();
  setGeneratorId(params.id);
  setGeneratorDescription(params.description);

  /** embedding **/
  if (mEmbedTree) {
    LOG(info) << "Embedding into: " << mEmbedFile->GetName()
              << " (" << mEmbedEntries << " events)";
    return FairPrimaryGenerator::Init();
  }

  /** base class init **/
  return FairPrimaryGenerator::Init();
}

/*****************************************************************/

Bool_t PrimaryGenerator::GenerateEvent(FairGenericStack* pStack)
{
  /** generate event **/

  /** normal generation if no embedding **/
  if (!mEmbedTree) {
    fixInteractionVertex(); // <-- always fixes vertex outside of FairROOT
    auto ret = FairPrimaryGenerator::GenerateEvent(pStack);
    if (ret) {
      setGeneratorInformation();
    }
    return ret;
  }

  /** this is for embedding **/

  /** setup interaction vertex **/
  mEmbedTree->GetEntry(mEmbedIndex);
  setInteractionVertex(mEmbedEvent);

  /** notify event generators **/
  auto genList = GetListOfGenerators();
  for (int igen = 0; igen < genList->GetEntries(); ++igen) {
    auto o2gen = dynamic_cast<Generator*>(genList->At(igen));
    if (o2gen) {
      o2gen->notifyEmbedding(mEmbedEvent);
    }
  }

  /** generate event **/
  if (!FairPrimaryGenerator::GenerateEvent(pStack)) {
    return kFALSE;
  }

  /** add embedding info to event header **/
  auto o2event = dynamic_cast<MCEventHeader*>(fEvent);
  if (o2event) {
    o2event->setEmbeddingFileName(mEmbedFile->GetName());
    o2event->setEmbeddingEventIndex(mEmbedIndex);
  }
  setGeneratorInformation();

  /** increment embedding counter **/
  mEmbedIndex++;
  mEmbedIndex %= mEmbedEntries;

  /** success **/
  return kTRUE;
}

/*****************************************************************/

void PrimaryGenerator::AddTrack(Int_t pdgid, Double_t px, Double_t py, Double_t pz,
                                Double_t vx, Double_t vy, Double_t vz,
                                Int_t mother1, Int_t mother2,
                                Int_t daughter1, Int_t daughter2,
                                Bool_t wanttracking,
                                Double_t e, Double_t tof,
                                Double_t weight, TMCProcess proc, Int_t generatorStatus)
{
  /** add track **/

  // check the status encoding
  if (!mcgenstatus::isEncoded(generatorStatus) && proc == TMCProcess::kPPrimary) {
    LOG(fatal) << "Generatror status " << generatorStatus << " of particle is not encoded properly.";
  }

  /** add event vertex to track vertex **/
  vx += fVertex.X();
  vy += fVertex.Y();
  vz += fVertex.Z();

  /** check if particle to be tracked exists in PDG database **/
  auto particlePDG = TDatabasePDG::Instance()->GetParticle(pdgid);
  if (wanttracking && !particlePDG) {
    LOG(warn) << "Particle to be tracked is not defined in PDG: pdg = " << pdgid;
    wanttracking = false;
  }

  /** set all other parameters required by PushTrack **/
  Int_t doTracking = 0; // Go to tracking
  if (fdoTracking && wanttracking) {
    doTracking = 1;
  }
  Int_t dummyparent = -1; // Primary particle (now the value is -1 by default)
  Double_t polx = 0.;     // Polarisation
  Double_t poly = 0.;
  Double_t polz = 0.;
  Int_t ntr = 0;                  // Track number; to be filled by the stack
  Int_t status = generatorStatus; // Generation status

  // correct for tracks which are in list before generator is called
  if (mother1 != -1) {
    mother1 += fMCIndexOffset;
  }
  if (mother2 != -1) {
    mother2 += fMCIndexOffset;
  }
  if (daughter1 != -1) {
    daughter1 += fMCIndexOffset;
  }
  if (daughter2 != -1) {
    daughter2 += fMCIndexOffset;
  }

  /** if it is a K0/antiK0 to be tracked, convert it into K0s/K0L.

      NOTE: we could think of pushing the K0/antiK0 without tracking first
      and then push she K0s/K0L for tracking.
      In this way we would properly keep track of this conversion,
      but there is the risk of messing up with the indices, so this
      is not done for the time being.
  **/
  if (abs(pdgid) == 311 && doTracking) {
    LOG(warn) << "K0/antiK0 requested for tracking: converting into K0s/K0L";
    pdgid = gRandom->Uniform() < 0.5 ? 310 : 130;
  }

  /** compute particle energy if negative **/
  if (e < 0) {
    double mass = particlePDG ? particlePDG->Mass() : 0.;
    e = std::sqrt(mass * mass + px * px + py * py + pz * pz);
  }

  /** add track to stack **/
  auto stack = dynamic_cast<o2::data::Stack*>(fStack);
  if (!stack) {
    LOG(fatal) << "Stack must be an o2::data:Stack";
    return; // must be the o2 stack
  }
  stack->PushTrack(doTracking, mother1, pdgid, px, py, pz,
                   e, vx, vy, vz, tof, polx, poly, polz, TMCProcess::kPPrimary, ntr,
                   weight, status, mother2, daughter1, daughter2, proc);

  fNTracks++;
}

/*****************************************************************/

void PrimaryGenerator::AddTrack(Int_t pdgid, Double_t px, Double_t py,
                                Double_t pz, Double_t vx, Double_t vy,
                                Double_t vz, Int_t parent, Bool_t wanttracking,
                                Double_t e, Double_t tof, Double_t weight, TMCProcess proc)
{
  // Do this to encode status code correctly. In FairRoot's PrimaryGenerator, this is simply one number that is set to 0.
  // So we basically do the same.
  // Assuming that this is treated as the HepMC status code down the line (as it used to be).
  AddTrack(pdgid, px, py, pz, vx, vy, vz, parent, -1, -1, -1, wanttracking,
           e, tof, weight, proc, mcgenstatus::MCGenStatusEncoding(0, 0).fullEncoding);
}

/*****************************************************************/

void PrimaryGenerator::setInteractionVertex(const MCEventHeader* event)
{
  /** set interaction vertex **/

  Double_t xyz[3] = {event->GetX(), event->GetY(), event->GetZ()};
  SetBeam(xyz[0], xyz[1], 0., 0.);
  SetTarget(xyz[2], 0.);
  SmearVertexXY(false);
  SmearVertexZ(false);
  SmearGausVertexXY(false);
  SmearGausVertexZ(false);
}

/*****************************************************************/
void PrimaryGenerator::setExternalVertexForNextEvent(double x, double y, double z)
{
  mExternalVertexX = x;
  mExternalVertexY = y;
  mExternalVertexZ = z;
  mHaveExternalVertex = true;
}

/*****************************************************************/

void PrimaryGenerator::setVertexMode(o2::conf::VertexMode const& mode, o2::dataformats::MeanVertexObject const* v)
{
  mVertexMode = mode;
  if (mode == o2::conf::VertexMode::kCCDB) {
    if (!v) {
      LOG(fatal) << "A valid MeanVertexObject needs to be passed with option o2::conf::VertexMode::kCCDB";
    }
    mMeanVertex = std::move(std::unique_ptr<o2::dataformats::MeanVertexObject>(new o2::dataformats::MeanVertexObject(*v)));
    LOG(info) << "The mean vertex is set to :";
    mMeanVertex->print();
  }
}

/*****************************************************************/

void PrimaryGenerator::fixInteractionVertex()
{
  // if someone gave vertex from outside; we will take it
  if (mHaveExternalVertex) {
    SetBeam(mExternalVertexX, mExternalVertexY, 0., 0.);
    SetTarget(mExternalVertexZ, 0.);
    mHaveExternalVertex = false; // the vertex is now consumed
    return;
  }

  // sampling a vertex and fixing for next event; no smearing will be done
  // inside FairPrimaryGenerator;
  SmearVertexXY(false);
  SmearVertexZ(false);
  SmearGausVertexXY(false);
  SmearGausVertexZ(false);

  // we use the mMeanVertexObject if initialized (initialize first)
  if (!mMeanVertex) {
    if (mVertexMode == o2::conf::VertexMode::kDiamondParam) {
      auto const& param = InteractionDiamondParam::Instance();
      const auto& xyz = param.position;
      const auto& sigma = param.width;
      mMeanVertex = std::move(std::unique_ptr<o2::dataformats::MeanVertexObject>(new o2::dataformats::MeanVertexObject(xyz[0], xyz[1], xyz[2], sigma[0], sigma[1], sigma[2], param.slopeX, param.slopeY)));
    }
    if (mVertexMode == o2::conf::VertexMode::kNoVertex) {
      mMeanVertex = std::move(std::unique_ptr<o2::dataformats::MeanVertexObject>(new o2::dataformats::MeanVertexObject(0, 0, 0, 0, 0, 0, 0, 0)));
    }
    if (mVertexMode == o2::conf::VertexMode::kCCDB) {
      // fatal.. then the object should have been passed with setting
      LOG(fatal) << "MeanVertexObject is null ... but mode is kCCDB. Please inject the valid CCDB object via setVertexMode";
    }
  }
  auto sampledvertex = mMeanVertex->sample();

  LOG(info) << "Sampled interacting vertex " << sampledvertex;
  SetBeam(sampledvertex.X(), sampledvertex.Y(), 0., 0.);
  SetTarget(sampledvertex.Z(), 0.);
}

/*****************************************************************/

Bool_t PrimaryGenerator::embedInto(TString fname)
{
  /** embed into **/

  /** check if a file is already open **/
  if (mEmbedFile && mEmbedFile->IsOpen()) {
    LOG(error) << "Another embedding file is currently open";
    return kFALSE;
  }

  /** open file **/
  mEmbedFile = TFile::Open(fname);
  if (!mEmbedFile || !mEmbedFile->IsOpen()) {
    LOG(error) << "Cannot open file for embedding: " << fname;
    return kFALSE;
  }

  /** get tree **/
  mEmbedTree = (TTree*)mEmbedFile->Get("o2sim");
  if (!mEmbedTree) {
    LOG(error) << R"(Cannot find "o2sim" tree for embedding in )" << fname;
    return kFALSE;
  }

  /** get entries **/
  mEmbedEntries = mEmbedTree->GetEntries();
  if (mEmbedEntries <= 0) {
    LOG(error) << "Invalid number of entries found in tree for embedding: " << mEmbedEntries;
    return kFALSE;
  }

  /** connect MC event header **/
  mEmbedEvent = new MCEventHeader;
  mEmbedTree->SetBranchAddress("MCEventHeader.", &mEmbedEvent);

  /** success **/
  return kTRUE;
}

/*****************************************************************/

void PrimaryGenerator::setGeneratorInformation()
{
  auto o2event = dynamic_cast<MCEventHeader*>(fEvent);
  if (o2event) {
    o2event->putInfo<int>(o2::mcgenid::GeneratorProperty::GENERATORID, mGeneratorId);
    o2event->putInfo<std::string>(o2::mcgenid::GeneratorProperty::GENERATORDESCRIPTION, mGeneratorDescription);
  }
}

/*****************************************************************/
/*****************************************************************/

} /* namespace eventgen */
} /* namespace o2 */

ClassImp(o2::eventgen::PrimaryGenerator);
