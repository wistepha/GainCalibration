
// -*- C++ -*-
//
// Package:    SiPixelGainCalibrationDBUploader
// Class:      SiPixelGainCalibrationDBUploader
// 
/**\class SiPixelGainCalibrationDBUploader SiPixelGainCalibrationDBUploader.cc CalibTracker/SiPixelGainCalibrationDBUploader/src/SiPixelGainCalibrationDBUploader.cc

Description: <one line class summary>

Implementation:
<Notes on implementation>
*/
//
// Original Author:  Freya BLEKMAN
//         Created:  Tue Aug  5 16:22:46 CEST 2008
// $Id: SiPixelGainCalibrationDBUploader.cc,v 1.7 2010/01/12 11:29:54 rougny Exp $
//
//


// system include files
#include <memory>
#include <fstream>
#include <sys/stat.h>

// user include files
#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/EDAnalyzer.h"

#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"

#include "FWCore/ServiceRegistry/interface/Service.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "CondFormats/SiPixelObjects/interface/SiPixelCalibConfiguration.h"
#include "CondFormats/SiPixelObjects/interface/SiPixelGainCalibration.h"
#include "CondFormats/SiPixelObjects/interface/SiPixelGainCalibrationOffline.h"
#include "CondFormats/SiPixelObjects/interface/SiPixelGainCalibrationForHLT.h"
#include "CalibTracker/SiPixelESProducers/interface/SiPixelGainCalibrationService.h"
#include "CondCore/DBOutputService/interface/PoolDBOutputService.h"

#include "Geometry/TrackerGeometryBuilder/interface/TrackerGeometry.h"
#include "Geometry/TrackerGeometryBuilder/interface/PixelGeomDetUnit.h"
#include "Geometry/Records/interface/TrackerDigiGeometryRecord.h"
#include "Geometry/CommonTopologies/interface/PixelTopology.h"

#include "TH2F.h"
#include "TFile.h"
#include "TDirectory.h"
#include "TKey.h"
#include "TString.h"
#include "TList.h"
#include "TTree.h"

#include "SiPixelGainCalibrationDBUploader.h"
#include "CommonTools/UtilAlgos/interface/TFileService.h"
#include "FWCore/ServiceRegistry/interface/Service.h"

//
// class decleration
//


void SiPixelGainCalibrationDBUploader::fillDatabase(const edm::EventSetup& iSetup){
  // only create when necessary.
  // process the minimum and maximum gain & ped values...
  std::cout << "Start filling db" << std::endl;
  
  std::map<uint32_t,std::pair<TString,int> > badresults;

  edm::Service<TFileService> fs;

  TH1F *VCAL_endpoint = fs->make<TH1F>("VCAL_endpoint","value where response = 255 ( x = (255 - ped)/gain )",256,0,256);
  TH1F *goodgains = fs->make<TH1F>("goodgains","gain values",100,0,10);
  TH1F *goodpeds = fs->make<TH1F>("goodpeds","pedestal values",356,-100,256);
  TH1F *totgains = fs->make<TH1F>("totgains","gain values",200,0,10);
  TH1F *totpeds = fs->make<TH1F>("totpeds","pedestal values",356,-100,256);
  TTree *tree = new TTree("tree","tree");
  int detidfortree,rowfortree,colfortree,useddefaultfortree;
  float pedfortree, gainfortree,chi2fortree;
  tree->Branch("detid",&detidfortree,"detid/I");
  tree->Branch("row",&rowfortree,"row/I");
  tree->Branch("col",&colfortree,"col/I");
  tree->Branch("defaultval",&useddefaultfortree,"defaultval/I");
  tree->Branch("ped",&pedfortree,"ped/F");
  tree->Branch("gain",&gainfortree,"gain/F");
  tree->Branch("chi2",&chi2fortree,"chi2/F");

  size_t ntimes=0;
  std::cout << "Filling record " << record_ << std::endl;
  if(record_!="SiPixelGainCalibrationForHLTRcd" && record_!="SiPixelGainCalibrationOfflineRcd"){
    std::cout << "you passed record " << record_ << ", which I have no idea what to do with!" << std::endl;
    return;
  }
  if(gainlow_>gainhi_){  
    float temp=gainhi_;
    gainhi_=gainlow_;
    gainlow_=temp;
  }
  if(pedlow_>pedhi_){  
    float temp=pedhi_;
    pedhi_=pedlow_;
    pedlow_=temp;
  }
  if(gainhi_>gainmax_)
    gainhi_=gainmax_;
  if(pedhi_>pedmax_)
    pedhi_=pedmax_;
  float badpedval=pedlow_-200;
  float badgainval=gainlow_-200;
  float meangain= meanGainHist_->GetMean();
  float meanped = meanPedHist_->GetMean();
  std::cout << "pedestals low: " << pedlow_ << " high: " << pedhi_ << " gains low: " << gainlow_ << " high: " << gainhi_
	    << ", and mean gain " << meangain<< ", ped " << meanped << std::endl;
  
  // and fill the dummy histos:
  
  for(size_t icol=0; icol<nmaxcols;++icol){
    for(size_t irow=0; irow<nmaxrows; ++irow){
      defaultGain_->SetBinContent(icol+1,irow+1,meangain);
      defaultPed_->SetBinContent(icol+1,irow+1,meanped);
      defaultChi2_->SetBinContent(icol+1,irow+1,1.0);
      defaultFitResult_->SetBinContent(icol+1,irow+1,0);
    }
  }
  
  theGainCalibrationDbInput_= new SiPixelGainCalibration(pedlow_*0.999,pedhi_*1.001,gainlow_*0.999,gainhi_*1.001);
  theGainCalibrationDbInputHLT_ = new SiPixelGainCalibrationForHLT(pedlow_*0.999,pedhi_*1.001,gainlow_*0.999,gainhi_*1.001);
  theGainCalibrationDbInputOffline_ = new SiPixelGainCalibrationOffline(pedlow_*0.999,pedhi_*1.001,gainlow_*0.999,gainhi_*1.001);
  
  
  uint32_t nchannels=0;
  uint32_t nmodules=0;
  uint32_t detid=0;
  therootfile_->cd();
  edm::ESHandle<TrackerGeometry> pDD;
  iSetup.get<TrackerDigiGeometryRecord>().get( pDD );     
  edm::LogInfo("SiPixelCondObjOfflineBuilder") <<" There are "<<pDD->dets().size() <<" detectors"<<std::endl;
  std::cout << "Start looping on detids, there are " << bookkeeper_.size() << " histograms to consider..." << std::endl;
  
  int NDetid = 0;
  for(TrackerGeometry::DetContainer::const_iterator it = pDD->dets().begin(); it != pDD->dets().end(); it++){
    ++nmodules;
    detid=0;
    if( dynamic_cast<PixelGeomDetUnit const*>((*it))!=0)
      detid=((*it)->geographicalId()).rawId();
    if(detid==0)
      continue;
    NDetid++;
    int badDetId=0;
    ntimes=0;
    useddefaultfortree=0;
    
    // Get the module sizes.
    TH2F *tempchi2;
    TH2F *tempfitresult;
    TH2F *tempgain;
    TH2F *tempped;
    TString tempgainstring;

    if(!badDetId){
    
      TString tempchi2string = bookkeeper_[detid]["chi2prob_2d"];
      tempchi2 = (TH2F*)therootfile_->Get(tempchi2string);
      if(tempchi2==0 || badDetId){
	tempchi2=defaultChi2_;
	useddefaultfortree=1;
      }
      TString tempfitresultstring = bookkeeper_[detid]["fitresult_2d"];
      tempfitresult = (TH2F*)therootfile_->Get(tempfitresultstring);
      if(tempfitresult==0){
	tempfitresult=defaultFitResult_;  
	useddefaultfortree=1;
      }
      TString tempgainstring = bookkeeper_[detid]["gain_2d"];
      tempgain = (TH2F*)therootfile_->Get(tempgainstring);
      if(tempgain==0){
	// std::cout <<"WARNING, gain histo " << bookkeeper_[detid]["gain_2d"] << " does not exist, using default instead" << std::endl;
	tempgain=defaultGain_;  
	useddefaultfortree=1;
      
      }
      TString temppedstring = bookkeeper_[detid]["ped_2d"];
      tempped = (TH2F*) therootfile_->Get(temppedstring);
      if(tempped==0){
	// std::cout <<"WARNING, ped histo " << bookkeeper_[detid]["ped_2d"] << " for detid " << detid << " does not exist, using default instead" << std::endl;
	std::pair<TString,int> tempval(tempgainstring,0);
	badresults[detid]=tempval;
	tempped=defaultPed_;
	useddefaultfortree=1;
      }
    }
    else {
      tempchi2=defaultChi2_;
      tempgain=defaultGain_; 
      tempfitresult=defaultFitResult_;  
      std::pair<TString,int> tempval(tempgainstring,0);
      badresults[detid]=tempval;
      tempped=defaultPed_;
      useddefaultfortree=1;
    }
    
    
    const PixelGeomDetUnit * pixDet  = dynamic_cast<const PixelGeomDetUnit*>((*it));
    const PixelTopology & topol = pixDet->specificTopology();       
    // Get the module sizes.
    size_t nrows = topol.nrows();      // rows in x
    size_t ncols = topol.ncolumns();   // cols in y
    
    // int nrows=tempgain->GetNbinsY();
    // int ncols=tempgain->GetNbinsX();
    // std::cout << "next histo " << tempgain->GetTitle() << " has nrow,ncol:" << nrows << ","<< ncols << std::endl;
    size_t nrowsrocsplit = theGainCalibrationDbInputHLT_->getNumberOfRowsToAverageOver();
    if(theGainCalibrationDbInputOffline_->getNumberOfRowsToAverageOver()!=nrowsrocsplit)
      throw  cms::Exception("GainCalibration Payload configuration error")
	<< "[SiPixelGainCalibrationAnalysis::fillDatabase] ERROR the SiPixelGainCalibrationOffline and SiPixelGainCalibrationForHLT database payloads have different settings for the number of rows per roc: " << theGainCalibrationDbInputHLT_->getNumberOfRowsToAverageOver() << "(HLT), " << theGainCalibrationDbInputOffline_->getNumberOfRowsToAverageOver() << "(offline)";
    std::vector<char> theSiPixelGainCalibrationPerPixel;
    std::vector<char> theSiPixelGainCalibrationPerColumn;
    std::vector<char> theSiPixelGainCalibrationGainPerColPedPerPixel;
    
    //Get mean of gain/pedestal of this Detid
    meangain= meanGainHist_->GetMean();
    meanped = meanPedHist_->GetMean();
    int npix=0;
    double meanGainForThisModule=0;
    double meanPedForThisModule=0;
    for(size_t icol=1; icol<=ncols; icol++){
      for(size_t jrow=1; jrow<=nrows; jrow++){
	if(tempfitresult->GetBinContent(icol,jrow)>0){
          npix++;
          meanGainForThisModule+=tempgain->GetBinContent(icol,jrow);
          meanPedForThisModule+=tempped->GetBinContent(icol,jrow);
	}
      }
    }
    if(npix!=0) meanPedForThisModule/=npix;
    if(npix!=0) meanGainForThisModule/=npix;
    if(usemeanwhenempty_){
      if(meanGainForThisModule>gainlow_ && meanGainForThisModule<gainhi_ && npix>100) meangain = meanGainForThisModule;
      if(meanPedForThisModule>pedlow_   && meanPedForThisModule<pedhi_   && npix>100) meanped  = meanPedForThisModule;
    }
    
    // Loop over columns and rows of this DetID
    float peds[160];
    float gains[160];
    float pedforthiscol[2];
    float gainforthiscol[2];
    int nusedrows[2];
    size_t nemptypixels=0;
    for(size_t icol=1; icol<=ncols; icol++) {
      nusedrows[0]=nusedrows[1]=0;
      pedforthiscol[0]=pedforthiscol[1]=0;
      gainforthiscol[0]=gainforthiscol[1]=0;
      for(size_t jrow=1; jrow<=nrows; jrow++) {
	size_t iglobalrow=0;
	if(jrow>nrowsrocsplit)
	  iglobalrow=1;
	peds[jrow]=badpedval;
	gains[jrow]=badgainval;
	float ped = tempped->GetBinContent(icol,jrow);
	float gain = tempgain->GetBinContent(icol,jrow);
	float chi2 = tempchi2->GetBinContent(icol,jrow);
	float fitresult = tempfitresult->GetBinContent(icol,jrow);

	if(ped>pedlow_ && gain>gainlow_ && ped<pedhi_ && gain<gainhi_ && (fitresult>0)){
	  ntimes++;
	  VCAL_endpoint->Fill((255 - ped)/gain);
	  peds[jrow]=ped;
	  gains[jrow]=gain;
	  pedforthiscol[iglobalrow]+=ped;
	  gainforthiscol[iglobalrow]+=gain;
	  nusedrows[iglobalrow]++;
	  goodpeds->Fill(ped);
	  goodgains->Fill(gain);
	  detidfortree=detid;
	  rowfortree=jrow-1;
	  colfortree=icol-1;
	  gainfortree=gain;
	  pedfortree=ped;
	  chi2fortree=chi2;
	  tree->Fill();
	} else{  
	  nemptypixels++;
	  if(usemeanwhenempty_){
	    peds[jrow]=meanped;
	    gains[jrow]=meangain;
	    std::pair<TString,int> tempval(tempgainstring,1);
	    badresults[detid]=tempval;
	  } else{
	    std::pair<TString,int> tempval(tempgainstring,2);
	    badresults[detid]=tempval;
	    // if everything else fails: set the gain & ped now to dead
	    peds[jrow]=badpedval;
	    gains[jrow]=badgainval;
	  }
	}
	totgains->Fill(gains[jrow]);
	totpeds->Fill(peds[jrow]);
      }// now collected all info, actually do the filling
      
      for(size_t jrow=1; jrow<=nrows; jrow++) {
	nchannels++;
	size_t iglobalrow=0;
	if(jrow>nrowsrocsplit)
	  iglobalrow=1;
	float ped=peds[jrow];
	float gain=gains[jrow];
	
	if( ped>pedlow_ && gain>gainlow_ && ped<pedhi_ && gain<gainhi_ ){
	  theGainCalibrationDbInput_->setData(ped, gain, theSiPixelGainCalibrationPerPixel);
	  theGainCalibrationDbInputOffline_->setDataPedestal(ped, theSiPixelGainCalibrationGainPerColPedPerPixel);
	} else {
	  theGainCalibrationDbInput_->setDeadPixel(theSiPixelGainCalibrationPerPixel);
	  theGainCalibrationDbInputOffline_->setDeadPixel(theSiPixelGainCalibrationGainPerColPedPerPixel);
	}

	if(jrow%nrowsrocsplit==0){
	  if(nusedrows[iglobalrow]>0){
	    pedforthiscol[iglobalrow]/=(float)nusedrows[iglobalrow];
	    gainforthiscol[iglobalrow]/=(float)nusedrows[iglobalrow];
	  } 
	  if(gainforthiscol[iglobalrow]>gainlow_ && gainforthiscol[iglobalrow]<gainhi_ && pedforthiscol[iglobalrow]>pedlow_ && pedforthiscol[iglobalrow]<pedhi_ ){// good 
	    //	    std::cout << "setting ped & col aves: " << pedforthiscol[iglobalrow] << " " <<  gainforthiscol[iglobalrow]<< std::endl;
	  } else{	
	    if(usemeanwhenempty_){
	      pedforthiscol[iglobalrow]=meanped;
	      gainforthiscol[iglobalrow]=meangain;
	      std::pair<TString,int> tempval(tempgainstring,3);
	      badresults[detid]=tempval;
	    } else{ //make dead
	      pedforthiscol[iglobalrow]=badpedval;
	      gainforthiscol[iglobalrow]=badgainval;
	      std::pair<TString,int> tempval(tempgainstring,4);
	      badresults[detid]=tempval;
	    }
	  }

	  if(gainforthiscol[iglobalrow]>gainlow_ && gainforthiscol[iglobalrow]<gainhi_ && pedforthiscol[iglobalrow]>pedlow_ && pedforthiscol[iglobalrow]<pedhi_ ){
	    theGainCalibrationDbInputOffline_->setDataGain(gainforthiscol[iglobalrow],nrowsrocsplit,theSiPixelGainCalibrationGainPerColPedPerPixel);
	    theGainCalibrationDbInputHLT_->setData(pedforthiscol[iglobalrow],gainforthiscol[iglobalrow],theSiPixelGainCalibrationPerColumn);
	  } else{
	    theGainCalibrationDbInputOffline_->setDeadColumn(nrowsrocsplit,theSiPixelGainCalibrationGainPerColPedPerPixel);
	    theGainCalibrationDbInputHLT_->setDeadColumn(nrowsrocsplit,theSiPixelGainCalibrationPerColumn);
	  }
	}
      }
    }

  
    SiPixelGainCalibration::Range range(theSiPixelGainCalibrationPerPixel.begin(),theSiPixelGainCalibrationPerPixel.end());
    SiPixelGainCalibrationForHLT::Range hltrange(theSiPixelGainCalibrationPerColumn.begin(),theSiPixelGainCalibrationPerColumn.end());
    SiPixelGainCalibrationOffline::Range offlinerange(theSiPixelGainCalibrationGainPerColPedPerPixel.begin(),theSiPixelGainCalibrationGainPerColPedPerPixel.end());
    
    // now start creating the various database objects
    if( !theGainCalibrationDbInput_->put(detid,range,ncols) )
      edm::LogError("SiPixelGainCalibrationAnalysis")<<"warning: detid already exists for Offline (gain per col, ped per pixel) calibration database"<<std::endl;
    if( !theGainCalibrationDbInputOffline_->put(detid,offlinerange,ncols) )
      edm::LogError("SiPixelGainCalibrationAnalysis")<<"warning: detid already exists for Offline (gain per col, ped per pixel) calibration database"<<std::endl;
    if(!theGainCalibrationDbInputHLT_->put(detid,hltrange, ncols) )
      edm::LogError("SiPixelGainCalibrationAnalysis")<<"warning: detid already exists for HLT (pedestal and gain per column) calibration database"<<std::endl;
  }
  // now printing out summary:
  size_t nempty=0;
  size_t ndefault=0;
  size_t ndead=0;
  size_t ncoldefault=0;
  size_t ncoldead=0;
  for(std::map<uint32_t,std::pair<TString,int> >::const_iterator ibad= badresults.begin(); ibad!=badresults.end(); ++ibad){
    uint32_t detid = ibad->first;
    if(badresults[detid].second==0){
      nempty++;
    } else if(badresults[detid].second==1){
      ndefault++;
    } else if(badresults[detid].second==2){
      std::cout << badresults[detid].first  ;
      std::cout << " has one or more dead pixels";
      std::cout << std::endl;
      ndead++;
    }
    else if(badresults[detid].second==3){
      ncoldefault++;
    }
    else if(badresults[detid].second==4){
      std::cout << badresults[detid].first  ;
      std::cout << " has one or more dead columns";
      ncoldead++;
      std::cout << std::endl;
    }
  }
  std::cout << nempty << " modules were empty and now have pixels filled with default values." << std::endl;
  std::cout << ndefault << " modules have pixels filled with default values." << std::endl;
  std::cout << ndead << " modules have pixels flagged as dead." << std::endl;
  std::cout << ncoldefault << " modules have columns filled with default values." << std::endl;
  std::cout << ncoldead << " modules have columns filled with dead values." << std::endl;
  std::cout << " ---> PIXEL Modules  " << nmodules  << "\n"
	    << " ---> PIXEL Channels " << nchannels << std::endl;

  std::cout << " --- writing to DB!" << std::endl;
  edm::Service<cond::service::PoolDBOutputService> mydbservice;
  if(!mydbservice.isAvailable() ){
    edm::LogError("db service unavailable");
    std::cout << "db service is unavailable" << std::endl;
    return;
  }
  else{
    if(record_=="SiPixelGainCalibrationForHLTRcd"){
      std::cout << "now doing SiPixelGainCalibrationForHLTRcd payload..." << std::endl;
      if( mydbservice->isNewTagRequest(record_) ){
	mydbservice->createNewIOV<SiPixelGainCalibrationForHLT>(
								theGainCalibrationDbInputHLT_,
								mydbservice->beginOfTime(),
								mydbservice->endOfTime(),
								"SiPixelGainCalibrationForHLTRcd");
      }
      else{

	mydbservice->appendSinceTime<SiPixelGainCalibrationForHLT>(
								   theGainCalibrationDbInputHLT_, 
								   mydbservice->currentTime(),
								   "SiPixelGainCalibrationForHLTRcd");
      
      }
    }
    else if (record_=="SiPixelGainCalibrationOfflineRcd"){
      std::cout << "now doing SiPixelGainCalibrationOfflineRcd payload..." << std::endl; 
      if( mydbservice->isNewTagRequest(record_) ){
	mydbservice->createNewIOV<SiPixelGainCalibrationOffline>(
								 theGainCalibrationDbInputOffline_,
								 mydbservice->beginOfTime(),
								 mydbservice->endOfTime(),
								 "SiPixelGainCalibrationOfflineRcd");
      }
      else{
	mydbservice->appendSinceTime<SiPixelGainCalibrationOffline>(
								    theGainCalibrationDbInputOffline_, 
								    mydbservice->currentTime(),
								    "SiPixelGainCalibrationOfflineRcd");
	
      }
    }
    edm::LogInfo(" --- all OK");
  } 
}

SiPixelGainCalibrationDBUploader::SiPixelGainCalibrationDBUploader(const edm::ParameterSet& iConfig):
  conf_(iConfig),
  appendMode_(conf_.getUntrackedParameter<bool>("appendMode",true)),
  theGainCalibrationDbInput_(0),
  theGainCalibrationDbInputOffline_(0),
  theGainCalibrationDbInputHLT_(0),
  theGainCalibrationDbInputService_(iConfig),
  record_(conf_.getUntrackedParameter<std::string>("record","SiPixelGainCalibrationOfflineRcd")),
  gainlow_(10.),gainhi_(0.),pedlow_(255.),pedhi_(-256),
  usemeanwhenempty_(conf_.getUntrackedParameter<bool>("useMeanWhenEmpty",false)),
  rootfilestring_(conf_.getUntrackedParameter<std::string>("inputrootfile","inputfile.root")),
  gainmax_(20),pedmax_(200),badchi2_(conf_.getUntrackedParameter<double>("badChi2Prob",0.01)),nmaxcols(10*52),nmaxrows(160)



{
  //now do what ever initialization is needed
  ::putenv((char*)"CORAL_AUTH_USER=me");
  ::putenv((char*)"CORAL_AUTH_PASSWORD=test");   
  meanGainHist_= new TH1F("meanGainHist","mean Gain Hist",500,0,gainmax_);
  meanPedHist_ = new TH1F("meanPedHist", "mean Ped Hist", 512,-200,pedmax_);
  defaultGain_=new TH2F("defaultGain","default gain, contains mean",nmaxcols,0,nmaxcols,nmaxrows,0,nmaxrows);// using dummy (largest) module size
  defaultPed_=new TH2F("defaultPed","default pedestal, contains mean",nmaxcols,0,nmaxcols,nmaxrows,0,nmaxrows);// using dummy (largest) module size
  defaultFitResult_=new TH2F("defaultFitResult","default fitresult, contains '0'",nmaxcols,0,nmaxcols,nmaxrows,0,nmaxrows);// using dummy (largest) module size
  defaultChi2_=new TH2F("defaultChi2","default chi2 probability, contains '1'",nmaxcols,0,nmaxcols,nmaxrows,0,nmaxrows);// using dummy (largest) module size
}


SiPixelGainCalibrationDBUploader::~SiPixelGainCalibrationDBUploader() {}


//
// member functions
//

// ------------ method called to for each event  ------------
void
SiPixelGainCalibrationDBUploader::analyze(const edm::Event& iEvent, const edm::EventSetup& iSetup)
{ 
  getHistograms();
  fillDatabase(iSetup);
  // empty but should not be called anyway
}

void SiPixelGainCalibrationDBUploader::beginRun(const edm::EventSetup& iSetup){
 
}
// ------------ method called once each job just before starting event loop  ------------
void SiPixelGainCalibrationDBUploader::beginJob() {}

// ------------ method called once each job just after ending the event loop  ------------
void SiPixelGainCalibrationDBUploader::endJob() {}

void SiPixelGainCalibrationDBUploader::getHistograms() {
  std::cout <<"Parsing file " << rootfilestring_ << std::endl;
  therootfile_ = new TFile(rootfilestring_.c_str());
  therootfile_->cd();
  
  TDirectory *dir = therootfile_->GetDirectory("siPixelGainCalibrationAnalysis");
  TList *list = dir->GetListOfKeys();

  TString comparestring = "Module";

  std::vector<TString> keylist;
  std::vector<TString> hist2list;
  std::vector<TString> dirlist;
  std::vector<TString> notdonelist;
  std::vector<int> nsubdirs;
  int ikey=0;
  int num_dir_list=list->GetEntries() ;

  
  for(ikey=0;ikey<num_dir_list;  ikey++){
    TKey *thekey = (TKey*)list->At(ikey);
    if(thekey==0)
      continue;
    TString keyname=thekey->GetName();
    TString keytype=thekey->GetClassName();
  
    if(keytype=="TDirectoryFile"){
      TString dirname=dir->GetPath();
      dirname+="/";
      dirname+=keyname;
      dir=therootfile_->GetDirectory(dirname);
  
      list=dir->GetListOfKeys();
      if(dirname.Contains(comparestring)){
	dirlist.push_back(dirname);
      } else {
	notdonelist.push_back(dirname);
	nsubdirs.push_back(-1);
      }
    }
  }
  size_t nempty=0;
  while(nempty!=notdonelist.size()){
    for(size_t idir=0; idir<notdonelist.size(); ++idir){
      if(nsubdirs[idir]==0)
	continue;
      dir = therootfile_->GetDirectory(notdonelist[idir]); 
      list= dir->GetListOfKeys(); 
      int ndirectories=0;
      for(ikey=0;ikey<list->GetEntries();  ikey++){
	TKey *thekey = (TKey*)list->At(ikey);
	if(thekey==0)
	  continue;
	TString keyname=thekey->GetName();
	TString keytype=thekey->GetClassName();
	if(keytype=="TDirectoryFile"){
	  TString dirname=dir->GetPath();
	  dirname+="/";
	  dirname+=keyname;
	  ndirectories++;
	  if(dirname.Contains(comparestring)){
	    dirlist.push_back(dirname);
	  } else{ 
	    notdonelist.push_back(dirname);
	    nsubdirs.push_back(-1);
	  }
	}
      }
      nsubdirs[idir]=ndirectories;
    }
    nempty=0;
    for(size_t i=0; i<nsubdirs.size(); i++){
      if(nsubdirs[i]!=-1)
	nempty++;
    }
  }
  
  for(size_t idir=0; idir<dirlist.size() ; ++idir){
    uint32_t detid = 1;
    
    dir = therootfile_->GetDirectory(dirlist[idir]);
    list = dir->GetListOfKeys();
    for(ikey=0;ikey<list->GetEntries();  ikey++){
      TKey *thekey = (TKey*)list->At(ikey);
      if(thekey==0)
	continue;
      TString keyname=thekey->GetName();
      TString keytype=thekey->GetClassName();
      if(keytype=="TH2F" && (keyname.BeginsWith("Gain2d")||keyname.BeginsWith("Pedestal2d")||keyname.BeginsWith("GainChi2Prob2d")||keyname.BeginsWith("GainFitResult2d"))){
	TString detidstring = keyname;
	detidstring.Remove(0,detidstring.Sizeof()-10);
	
	detid = atoi(detidstring.Data());
	  
	if(keyname.BeginsWith("GainChi2Prob2d")){

	  TString tempstr =dirlist[idir];
	  tempstr+="/";
	  tempstr+=keyname;
	  TString replacestring = rootfilestring_;
	  replacestring+=":";
	  tempstr.ReplaceAll(replacestring,"");
	  bookkeeper_[detid]["chi2prob_2d"]=tempstr;
	} else if(keyname.BeginsWith("GainFitResult2d")){
	  TString tempstr =dirlist[idir];
	  tempstr+="/";
	  tempstr+=keyname;
	  TString replacestring = rootfilestring_;
	  replacestring+=":";
	  tempstr.ReplaceAll(replacestring,"");
	  bookkeeper_[detid]["fitresult_2d"]=tempstr;
	} else if(keyname.BeginsWith("Gain2d")){
	  std::map<std::string,TString> tempmap;
	  TString tempstr =dirlist[idir];
	  tempstr+="/";
	  tempstr+=keyname;
	  TString replacestring = rootfilestring_;
	  replacestring+=":";
	  tempstr.ReplaceAll(replacestring,"");
	  bookkeeper_[detid]["gain_2d"]=tempstr;
	}
	if(keyname.BeginsWith("Pedestal2d")){
	  std::map<std::string,TString> tempmap;
	  TString tempstr =dirlist[idir];
	  tempstr+="/";
	  tempstr+=keyname;
	  TString replacestring = rootfilestring_;
	  replacestring+=":";
	  tempstr.ReplaceAll(replacestring,"");
	  bookkeeper_[detid]["ped_2d"]=tempstr;
	}
      }
    }
    
    TH2F *temphistoped  = (TH2F*)therootfile_->Get(bookkeeper_[detid]["ped_2d"]);
    TH2F *temphistogain = (TH2F*)therootfile_->Get(bookkeeper_[detid]["gain_2d"]);
    TH2F *temphistofitresult = (TH2F*)therootfile_->Get(bookkeeper_[detid]["gain_2d"]);
	
    for(int xbin=1; xbin<=temphistoped->GetNbinsX(); ++xbin){
      for(int ybin=1; ybin<=temphistoped->GetNbinsY(); ++ybin){
	if(temphistofitresult->GetBinContent(xbin,ybin)<=0)
	  continue;
	float val = temphistoped->GetBinContent(xbin,ybin);
	if(val>pedmax_)
	  continue;
	if(pedlow_>val)
	  pedlow_=val;
	if(pedhi_<val)
	  pedhi_=val;
	meanPedHist_->Fill(val);
      }
    }
	  
    for(int xbin=1; xbin<=temphistogain->GetNbinsX(); ++xbin){
      for(int ybin=1; ybin<=temphistogain->GetNbinsY(); ++ybin){
	if(temphistofitresult->GetBinContent(xbin,ybin)<=0)
	  continue;
	float val = temphistogain->GetBinContent(xbin,ybin);
	if(val<=0.0001)
	  continue;
	if(gainlow_>val)
	  gainlow_=val;
	if(gainhi_<val)
	  gainhi_=val;
	meanGainHist_->Fill(val);
      }
    }
  }// end of loop over dirlist
}

//define this as a plug-in
DEFINE_FWK_MODULE(SiPixelGainCalibrationDBUploader);
