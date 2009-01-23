#define BGNAME "expBG"

#include "OverlayBX.h"
#include <iostream>

#ifdef MARLIN_USE_AIDA
#include <marlin/AIDAProcessor.h>
#include <AIDA/IHistogramFactory.h>
#include <AIDA/ICloud1D.h>
//#include <AIDA/IHistogram1D.h>
#endif

#include <EVENT/LCCollection.h>
#include <IMPL/LCCollectionVec.h>
#include <EVENT/MCParticle.h>
#include <EVENT/SimTrackerHit.h>
#include "IO/LCReader.h"
#include "UTIL/LCTOOLS.h"
#include "Merger.h"

#include <marlin/Global.h>
#include <gear/GEAR.h>
#include <gear/VXDParameters.h>
#include <gear/VXDLayerLayout.h>


#include "CLHEP/Random/RandFlat.h"
// #include "CLHEP/Vector/TwoVector.h"
// #include <time.h>

using namespace lcio ;
using namespace marlin ;


OverlayBX aOverlayBX ;


OverlayBX::OverlayBX() : Processor("OverlayBX") {

  // modify processor description
  _description = "Overlays background lcio files for many bunch crossings " ;


  // register steering parameters: name, description, class-variable, default value

  //   StringVec   _inputFileNames ;
  //   int         _eventsPerBX;

  //   float       _bxTime_s ;
  //   int         _maxBXsTPC ;
  //   float       _tpcVdrift_mm_s ;
  //   FloatVec    _vxdLayerReadOutTimes ;
  //   std::string _tpcCollection ;
  //   std::string _vxdCollection ;
  //   StringVec   _mergeCollections ;
  //   int         _ranSeed  ;
  //   std::map<std::string, std::string> _colMap;
  //   std::vector< LCReader* > _lcReaders ;
  //   int _nRun ;
  //   int _nEvt ;

  StringVec files ;
  files.push_back( "overlay.slcio" )  ;
  registerProcessorParameter( "InputFileNames" , 
			      "Name of the lcio input file(s)"  ,
			      _inputFileNames ,
			      files ) ;

  registerProcessorParameter( "NumberOfEventsPerBX" , 
			      "Fixed number of background events that are used for one bunch crossing." ,
			      _eventsPerBX ,
			      int(0) ) ;

  registerProcessorParameter( "BunchCrossingTime" , 
			      "time between bunch crossings [s] - default 3.0e-7 (300 ns)" ,
			      _bxTime_s ,
			      float(3.0e-7) ) ;

  registerProcessorParameter( "TPCDriftvelocity" , 
			      "[mm/s] (float) - default 5.0e+7 (5cm/us)" ,
			      _tpcVdrift_mm_s ,
			      float(5.0e+7) ) ;

  registerProcessorParameter( "MaxBXsTPC" , 
			      "maximum of BXs to be overlayed for the TPC; -1: compute from length"
			      " and BXtime; default 10" ,
			      _maxBXsTPC ,
			      int(10) ) ;

  FloatVec vxdTimes ;
  vxdTimes.push_back( 50. ) ;
  vxdTimes.push_back( 50. ) ;
  vxdTimes.push_back( 200. ) ;
  vxdTimes.push_back( 200. ) ;
  vxdTimes.push_back( 200. ) ;
  vxdTimes.push_back( 200. ) ;
  
  registerProcessorParameter( "VXDLayerReadOutTimes" , 
			      "readout time per layer in us - default 50. 50. 200. 200. 200. 200"  ,
			      _vxdLayerReadOutTimes ,
			      vxdTimes ) ;
  
  
  registerProcessorParameter( "TPCCollection" , 
			      "collection of TPC SimTrackerHits" ,
			      _tpcCollection,
			      std::string("TPCCollection") ) ;
  
  registerProcessorParameter( "VXDCollection" , 
			      "collection of VXD SimTrackerHits" ,
			      _vxdCollection,
			      std::string("VXDCollection") ) ;
  
  registerProcessorParameter( "RandomSeed" , 
			      "random seed - default 42" ,
			      _ranSeed ,
			      int(42) ) ;
  
  StringVec exMap;
  exMap.push_back( "mcParticles mcParticlesBG" );


  registerOptionalParameter( "MergeCollections" , 
			     "Pairs of collection with one bx to be overlayed (merged)"  ,
			     _mergeCollections ,
			     exMap ) ;

}



void OverlayBX::init() { 


  //FIXME: have new LCIO version with readRandomEvent()
  if( ! LCIO_VERSION_GE( 1 , 4 ) ) {
    throw Exception("  OverlayBX requires LCIO v1.4 or higher \n"
		    "  - please upgrade your LCIO version or disable the OverlayBX processor ! ") ;
  }

  // usually a good idea to
  printParameters() ;

  // opening background input files
  _lcReaders.resize( _inputFileNames.size() ) ;

  for( unsigned i=0 ; i < _inputFileNames.size() ; ++i ) {

    _lcReaders[i] = LCFactory::getInstance()->createLCReader() ;

    streamlog_out( DEBUG4 ) << " opening file for overlay : " << _inputFileNames[i]  << std::endl ;

    _lcReaders[i]->open( _inputFileNames[i]  ) ; 
  }

  // initalisation of random number generator
  CLHEP::HepRandom::setTheSeed( _ranSeed ) ;

  // preparing colleciton map for merge
  StringVec::iterator it;
  StringVec::iterator endIt = _mergeCollections.end();

  int oddNumOfCols = _mergeCollections.size() & 0x1;
  if (oddNumOfCols) { 
    streamlog_out( WARNING ) << "Odd number of collection names, last collection ignored." << std::endl;
    --endIt;
  }

  streamlog_out( DEBUG ) << " merging following collections from background to physics collections: " 
			 << std::endl  ;

  for (it=_mergeCollections.begin(); it < endIt; ++it) {  // treating pairs of collection names

    streamlog_out( DEBUG ) << "    " << *it << "  -> " ;

    std::string  key = (*it);  //src
    _colMap[key] = *(++it) ;// src -> dest

    streamlog_out( DEBUG ) << "    " << *it << std::endl ;

  }


  streamlog_out( WARNING ) << " need to compute the max numbers of BXs to be overlaid ..."  
			   << std::endl  ;

  init_geometry() ;


  streamlog_out( MESSAGE ) << " --- pair background in VXD detector : " << std::endl ;
  
  for( unsigned i=0 ; i < _vxdLayers.size() ; ++i){
    
    streamlog_out( MESSAGE ) << " for layer " << i << " overlay " <<   _vxdLayers[i].nBX  << " BXs of pair bg " 
			     << std::endl;

  }


  _nRun = 0 ;
  _nEvt = 0 ;
}



void OverlayBX::processRunHeader( LCRunHeader* run) { 

  _nRun++ ;
} 



LCEvent*  OverlayBX::readNextEvent(){
  
  int nRdr = _lcReaders.size() ;
  int iRdr = (int) ( CLHEP::RandFlat::shoot() * nRdr ) ; 

  streamlog_out( DEBUG ) << " reading next event from reader " << iRdr 
			 << " of " << nRdr  << std::endl ;

  //FIXME: need to read random events from this file

  LCEvent* evt = _lcReaders[iRdr]->readNextEvent( LCIO::UPDATE ) ;

  if( evt == 0 ) { // for now just close and reopen the file

    streamlog_out( MESSAGE ) << " ------ reopen reader " << iRdr 
			     << " of " << nRdr  << std::endl ;


    _lcReaders[iRdr]->close() ; 
    _lcReaders[iRdr]->open( _inputFileNames[iRdr]  ) ; 
    
    evt = _lcReaders[iRdr]->readNextEvent( LCIO::UPDATE ) ;
  }

  return evt ;
}


void OverlayBX::modifyEvent( LCEvent * evt ) {
  
  // require the MCParticle collection to be available
  LCCollection* mcpCol = 0 ; 
  try { 

    mcpCol = evt->getCollection( "MCParticle" ) ; // FIXME; make this a parameter

  } catch( DataNotAvailableException& e){ 

    streamlog_out( ERROR ) << " No MCParticle collection in event nr " << evt->getRunNumber() 
			   << " - " << evt->getEventNumber() 
			   << "  can't overlay backgrund ..." << std::endl ;
    return ;
  }





  long num = _eventsPerBX;

  // get number of BX as max from VXD layers:
  int numBX = 1 ;
  for( unsigned i=0 ; i < _vxdLayers.size() ; ++i){
    if( _vxdLayers[i].nBX > numBX ) 
      numBX = _vxdLayers[i].nBX ;
  }
  //  numBX = 10 ; // FIXME: debug


  streamlog_out( DEBUG ) << "** Processing event nr " << evt->getEventNumber() 
			 << "\n overlaying " << numBX << " bunchcrossings with " 
			 << num << " background events each." << std::endl;
  
  
  LCCollection* vxdCol = 0 ; 

  try { 
    
    vxdCol = evt->getCollection( _vxdCollection ) ;
    
  } catch( DataNotAvailableException& e) {
    
    // make sure there is a VXD collection in the event
    streamlog_out( DEBUG ) << " created new vxd hit collection " <<  _vxdCollection 
			   << std::endl ;
    
    vxdCol = new LCCollectionVec( LCIO::SIMTRACKERHIT )  ;
    evt->addCollection(  vxdCol , _vxdCollection  ) ;
  }
  
  

  int nVXDHits = 0 ;
  
  //  for(int i = -numBX  ; i <=numBX  ; i++ ) { 
  for(int i = 0  ; i <numBX  ; i++ ) {
    
    // loop over events in one BX ......
    for(long j=0; j < num  ; j++ ) {
      
      LCEvent* olEvt  = readNextEvent() ;
      
      streamlog_out( DEBUG ) << " merge bg event for BX  " << i << " :" 
			     << olEvt->getRunNumber()  << "  - "
			     << olEvt->getEventNumber()  
			     << std::endl;
      
      try { 
	
	LCCollection* vxdBGCol = olEvt->getCollection( _vxdCollection ) ;
	
	nVXDHits += mergeVXDColsFromBX( vxdCol , vxdBGCol , i )  ;
 
      } catch( DataNotAvailableException& e) {}
      

      try { 

	LCCollection* mcpBGCol = olEvt->getCollection( "MCParticle" ) ;
	
	Merger::merge( mcpBGCol,  mcpCol  )  ;
 
      } catch( DataNotAvailableException& e) {}




      if( i==0 ) { // merge hits of detectors w/ high time resolution for one  BX   

	Merger::merge( olEvt, evt, &_colMap ) ;
      }
    }
  }

  streamlog_out( DEBUG ) << " total number of VXD bg hits: " << nVXDHits 
			 << std::endl ;
	

  _nEvt ++ ;
}


int OverlayBX::mergeVXDColsFromBX( LCCollection* vxdCol , LCCollection* vxdBGCol , int bxNum ) {
  
  // FIXME: here we should shift the hits in r-phi along the ladder 
  // ... to be done ....
  
  const string destType = vxdCol->getTypeName();
  int nHits = 0 ;
  
  // check if collections have the same type
  if (destType != vxdBGCol->getTypeName()) {
    streamlog_out( WARNING ) << "merge not possible, collections of different type" << endl;
    return nHits ;
  }
  

  if ( destType == LCIO::SIMTRACKERHIT  )  {
    
    // running trough all the elements in the collection.
    nHits = vxdBGCol->getNumberOfElements();
    
    streamlog_out( DEBUG ) << " merging VXD hits from bg : " << nHits << endl;
    
    for (int i=nHits-1; i>=0; i--){ 
      // loop from back in order to remove vector elements from end ...
      
      SimTrackerHit* sth = dynamic_cast<SimTrackerHit*>(  vxdBGCol->getElementAt(i) ) ;

      LCObject* bgHit = vxdBGCol->getElementAt(i) ;

      vxdBGCol->removeElementAt(i);

      int layer = ( sth->getCellID() & 0xff )  ;
      
      if( bxNum < _vxdLayers[ layer-1 ].nBX  ) {

	vxdCol->addElement( bgHit );

      } else {

	// if hit not added we need to delete it as we removed from the collection (vector) 
	delete bgHit ;
      }

    }

  } else {
    
    streamlog_out( ERROR ) << " mergeVXDColsFromBX : wrong collection type  : " << destType  << endl;

  }

  return nHits ;
}


void OverlayBX::check( LCEvent * evt ) { 


#ifdef MARLIN_USE_AIDA
  struct H1D{
    enum { 
      hitsLayer1,
      hitsLayer2,
      hitsLayer3,
      hitsLayer4,
      hitsLayer5,
      hitsLayer6,
      size 
    }  ;
  };

  if( isFirstEvent() ) {
    
    _hist1DVec.resize( H1D::size )   ;
    
    float  hitMax =  100000. ;
    _hist1DVec[ H1D::hitsLayer1 ] = AIDAProcessor::histogramFactory(this)->createHistogram1D( "hitsLayer1", 
											      "hits Layer 1", 
											      100, 0. ,hitMax ) ; 
    _hist1DVec[ H1D::hitsLayer2 ] = AIDAProcessor::histogramFactory(this)->createHistogram1D( "hitsLayer2", 
											      "hits Layer 2", 
											      100, 0. , hitMax ) ; 
    _hist1DVec[ H1D::hitsLayer3 ] = AIDAProcessor::histogramFactory(this)->createHistogram1D( "hitsLayer3", 
											      "hits Layer 3", 
											      100, 0. , hitMax ) ; 
    _hist1DVec[ H1D::hitsLayer4 ] = AIDAProcessor::histogramFactory(this)->createHistogram1D( "hitsLayer4", 
											      "hits Layer 4", 
											      100, 0. ,hitMax ) ; 
    _hist1DVec[ H1D::hitsLayer5 ] = AIDAProcessor::histogramFactory(this)->createHistogram1D( "hitsLayer5", 
											      "hits Layer 5", 
											      100, 0. , hitMax ) ; 
    _hist1DVec[ H1D::hitsLayer6 ] = AIDAProcessor::histogramFactory(this)->createHistogram1D( "hitsLayer6", 
											      "hits Layer 6", 
											      100, 0. , hitMax ) ; 
  }
#endif



  LCCollection* vxdCol = 0 ; 

  int nhit = 0 ;
  int nHitL1 = 0 ;
  int nHitL2 = 0 ;
  int nHitL3 = 0 ;
  int nHitL4 = 0 ;
  int nHitL5 = 0 ;
  int nHitL6 = 0 ;

  
  try { 
    vxdCol = evt->getCollection( _vxdCollection ) ;

    int nH = vxdCol->getNumberOfElements() ;
    
    for(int i=0; i<nH ; ++i){
      
      SimTrackerHit* sth = dynamic_cast<SimTrackerHit*>(  vxdCol->getElementAt(i) ) ;


      int layer = ( sth->getCellID() & 0xff )  ;
      
      if( layer == 1 ) nHitL1++ ; 
      else if( layer == 2 ) nHitL2++ ; 
      else if( layer == 3 ) nHitL3++ ; 
      else if( layer == 4 ) nHitL4++ ; 
      else if( layer == 5 ) nHitL5++ ; 
      else if( layer == 6 ) nHitL6++ ; 
      

      MCParticle* mcp = sth->getMCParticle() ;
      if( mcp != 0  ){
	nhit++ ;
      }

    }
    if( nhit != nH ) {
      streamlog_out( ERROR ) << " found " << nhit << " MCParticles for " << nH 
			     << " SimTrackerHits "  << std::endl ; 
    } else {
      streamlog_out( DEBUG ) << " OK ! - found " << nhit << " MCParticle links for " << nH 
			     << " SimTrackerHits "  << std::endl ; 
    }
    

  } catch( DataNotAvailableException& e) {
  }
  
#ifdef MARLIN_USE_AIDA
  _hist1DVec[ H1D::hitsLayer1 ]->fill( nHitL1 ) ;
  _hist1DVec[ H1D::hitsLayer2 ]->fill( nHitL2 ) ;
  _hist1DVec[ H1D::hitsLayer3 ]->fill( nHitL3 ) ;
  _hist1DVec[ H1D::hitsLayer4 ]->fill( nHitL4 ) ;
  _hist1DVec[ H1D::hitsLayer5 ]->fill( nHitL5 ) ;
  _hist1DVec[ H1D::hitsLayer6 ]->fill( nHitL6 ) ;
#endif
  
}


void OverlayBX::end(){ 
  
  // close all open input files
  for( unsigned i=0 ; i < _lcReaders.size() ; ++i ) {
    _lcReaders[i]->close() ;
  }

  streamlog_out( MESSAGE ) << " overlayed pair background in VXD detector : " << std::endl ;

  for( unsigned i=0 ; i < _vxdLayers.size() ; ++i){

    streamlog_out( MESSAGE ) << " layer " << i << " overlayed " <<   _vxdLayers[i].nBX  << " BXs of pair bg " 
			     << std::endl;

    double area =  _vxdLayers[i].ladderArea * _vxdLayers[i].nLadders ;


    streamlog_out( MESSAGE ) << " -> average number of hits:  " 
			     <<  _hist1DVec[ i ]->mean() 
			     << " -    hits/ mm " <<  _hist1DVec[ i ]->mean() / area  
			     << " -    occupancy (25mu) " << _hist1DVec[ i ]->mean() / area / 160. 
			     << std::endl ;
    

    


  }
  
}





void OverlayBX::init_geometry(){

  //get VXD geometry info
  const gear::VXDParameters& gearVXD = Global::GEAR->getVXDParameters() ;
  const gear::VXDLayerLayout& layerVXD = gearVXD.getVXDLayerLayout(); 
  
  if( (unsigned) layerVXD.getNLayers() !=   _vxdLayerReadOutTimes.size()  ){
    
    
    streamlog_out( ERROR  ) << " *************************************************** " << std::endl 
                            << " wrong number of readout times: " <<  _vxdLayerReadOutTimes.size() 
                            << " for " << layerVXD.getNLayers() << " VXD layers "
      //                            << "  - do nothing ! " << std::endl 
                            << " *************************************************** " << std::endl  ;

  }

  _vxdLadders.resize( layerVXD.getNLayers() ) ; 
  _vxdLayers.resize(  layerVXD.getNLayers() ) ; 


  
  streamlog_out( DEBUG ) << " initializing VXD ladder geometry ... " << std::endl ;
    
  // get the ladder's geometry parameters
  for( int i=0 ; i <  layerVXD.getNLayers() ; i++ ) {
    
    double 	phi0 = layerVXD.getPhi0 (i) ;    
    double 	dist = layerVXD.getSensitiveDistance (i) ;
    
    double 	thick = layerVXD.getSensitiveThickness (i) ;
    double 	offs =  layerVXD.getSensitiveOffset (i) ;
    double 	width = layerVXD.getSensitiveWidth (i) ;
    
    // -----fg:  gear length is half length really !!!!!
    double 	len =   2 * layerVXD.getSensitiveLength (i) ; 
    
  
    int nLad  = layerVXD.getNLadders(i) ;


    _vxdLayers[i].nBX = (int) ( _vxdLayerReadOutTimes[i]*1.e-6 / _bxTime_s ) ;
    _vxdLayers[i].width = width ;
    _vxdLayers[i].ladderArea = width * len ;
    _vxdLayers[i].nLadders = nLad ;
    
    streamlog_out( DEBUG ) << " layer: " << i 
			   << " phi0 : " << phi0
			   << " offs : " << offs
			   << " width : " << width
			   << " nBX : " << _vxdLayers[i].nBX
			   << std::endl ;


    _vxdLadders[i].resize( nLad ) ;

    for( int j=0 ; j < nLad ; j++ ) {

      double phi = phi0 + j *  ( 2 * M_PI ) /  nLad  ; 

      // point in middle of sensitive ladder  (w/o offset)
      CLHEP::Hep2Vector pM ;
      pM.setPolar(  dist + thick / 2. , phi ) ;
      
      // direction vector along ladder in rphi (negative) 
      CLHEP::Hep2Vector v0 ;
      v0.setPolar( width /2. + offs  ,  phi +  M_PI / 2.) ;

      // v0.setPolar(   - (width / 2.  - offs ) ,  phi +  M_PI / 2.) ;
      // point p1:   'lower left corner of sensitive surface' (seen from outside)
      // gear::Vector3D p1 = p0 + v0 ;    
      
      
      CLHEP::Hep2Vector  p0 = pM + v0 ;  

      // v1: direction along  rphi
      CLHEP::Hep2Vector v1 ;
      v1.setPolar( width ,  phi - M_PI/2.) ;

      // other end of ladder
      CLHEP::Hep2Vector  p1 = p0 + v1 ;  


      VXDLadder& l = _vxdLadders[i][j] ;
      l.phi = phi ;
      l.p0 = p0 ;
      l.p1 = p1 ;
      l.u  = v1.unit()  ;
      
      streamlog_out( DEBUG ) << " layer: " << i << " - ladder: " << j 
			     << " phi : " << l.phi
			     << " p0 : "  << l.p0 
			     << " p1 : "  << l.p1 
			     << " u  : "  << l.u 
			     << " width: " << v1.mag() 
			     << " dist: " << pM.r() 
                             << std::endl ;

    }
  }
  
  return ;
}