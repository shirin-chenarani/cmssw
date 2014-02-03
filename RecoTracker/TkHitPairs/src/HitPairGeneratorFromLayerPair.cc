#include "RecoTracker/TkHitPairs/interface/HitPairGeneratorFromLayerPair.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"

#include "TrackingTools/DetLayers/interface/DetLayer.h"
#include "TrackingTools/DetLayers/interface/BarrelDetLayer.h"
#include "TrackingTools/DetLayers/interface/ForwardDetLayer.h"

#include "RecoTracker/TkTrackingRegions/interface/HitRZCompatibility.h"
#include "RecoTracker/TkTrackingRegions/interface/TrackingRegion.h"
#include "RecoTracker/TkTrackingRegions/interface/TrackingRegionBase.h"
#include "RecoTracker/TkHitPairs/interface/OrderedHitPairs.h"
#include "RecoTracker/TkHitPairs/src/InnerDeltaPhi.h"

#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "RecoTracker/TkSeedingLayers/interface/SeedingLayer.h"

using namespace GeomDetEnumerators;
using namespace ctfseeding;
using namespace std;

typedef PixelRecoRange<float> Range;

namespace {
  template<class T> inline T sqr( T t) {return t*t;}
}


#include "TrackingTools/TransientTrackingRecHit/interface/TransientTrackingRecHitBuilder.h"
#include "TrackingTools/Records/interface/TransientRecHitRecord.h"
#include "Geometry/TrackerGeometryBuilder/interface/TrackerGeometry.h"
#include "RecoTracker/Record/interface/TrackerRecoGeometryRecord.h"
#include "FWCore/Framework/interface/ESHandle.h"

HitPairGeneratorFromLayerPair::HitPairGeneratorFromLayerPair(
							     const Layer& inner, 
							     const Layer& outer, 
							     LayerCacheType* layerCache,
							     unsigned int nSize,
							     unsigned int max)
  : HitPairGenerator(nSize),
    theLayerCache(*layerCache), theOuterLayer(outer), theInnerLayer(inner)
{
  theMaxElement=max;
  debug=false;
  idIn=0;
  idOut=0;
}


// devirtualizer
#include<tuple>
namespace {

  template<typename Algo>
  struct Kernel {
    using  Base = HitRZCompatibility;
    void set(Base const * a) {
      assert( a->algo()==Algo::me);
      checkRZ=reinterpret_cast<Algo const *>(a);
    }
    
    void operator()(int b, int e, const RecHitsSortedInPhi & innerHitsMap, bool * ok) const {
      constexpr float nSigmaRZ = std::sqrt(12.f);
      for (int i=b; i!=e; ++i) {
	Range allowed = checkRZ->range(innerHitsMap.u[i]);
	float vErr = nSigmaRZ * innerHitsMap.dv[i];
	Range hitRZ(innerHitsMap.v[i]-vErr, innerHitsMap.v[i]+vErr);
	Range crossRange = allowed.intersection(hitRZ);
	ok[i-b] = ! crossRange.empty() ;
      }
    }
    Algo const * checkRZ;
    
  };


  template<typename ... Args> using Kernels = std::tuple<Kernel<Args>...>;

}



void HitPairGeneratorFromLayerPair::hitPairs(
					     const TrackingRegion & region, OrderedHitPairs & result,
					     const edm::Event& iEvent, const edm::EventSetup& iSetup) {
  
  auto const & ds = doublets(region,iEvent,iSetup);
  for (std::size_t i=0; i!=ds.size(); ++i) {
    result.push_back( OrderedHitPair( ds.hit(i,HitDoublets::inner),ds.hit(i,HitDoublets::outer) ));
  }
  if (theMaxElement!=0 && result.size() >= theMaxElement){
     result.clear();
    edm::LogError("TooManyPairs")<<"number of pairs exceed maximum, no pairs produced";
  }
}


HitDoublets HitPairGeneratorFromLayerPair::doublets( const TrackingRegion& region, 
						    const edm::Event & iEvent, const edm::EventSetup& iSetup) {

  typedef OrderedHitPair::InnerRecHit InnerHit;
  typedef OrderedHitPair::OuterRecHit OuterHit;
  typedef RecHitsSortedInPhi::Hit Hit;

  const RecHitsSortedInPhi & innerHitsMap = theLayerCache(&theInnerLayer, region, iEvent, iSetup);
  if (innerHitsMap.empty()) return HitDoublets(innerHitsMap,innerHitsMap);
 
  const RecHitsSortedInPhi& outerHitsMap = theLayerCache(&theOuterLayer, region, iEvent, iSetup);
  if (outerHitsMap.empty()) return HitDoublets(innerHitsMap,outerHitsMap);

  HitDoublets result(innerHitsMap,outerHitsMap); result.reserve(std::max(innerHitsMap.size(),outerHitsMap.size()));

  InnerDeltaPhi deltaPhi(*theOuterLayer.detLayer(), *theInnerLayer.detLayer(), region, iSetup);

  // std::cout << "layers " << theInnerLayer.detLayer()->seqNum()  << " " << theOuterLayer.detLayer()->seqNum() << std::endl;

  // constexpr float nSigmaRZ = std::sqrt(12.f);
  constexpr float nSigmaPhi = 3.f;
  for (int io = 0; io!=int(outerHitsMap.theHits.size()); ++io) { 
    Hit const & ohit =  outerHitsMap.theHits[io].hit();
    if (debug) {
      unsigned int theIdOut = ohit->rawId();
      if (idOut==theIdOut) {
	cout << "processing hit with id=" << idOut << " and position=" << ohit->globalPosition() 
		   << " phi=" << ohit->globalPosition().phi() 
		   << " z=" << ohit->globalPosition().z() 
		   << " r=" << ohit->globalPosition().perp() 
		   << " trans: " << (ohit->transientHits().size()>1 ? ohit->transientHits()[0]->globalPosition() : GlobalPoint(0,0,0)) << " "
		   << (ohit->transientHits().size()>1 ? ohit->transientHits()[1]->globalPosition() : GlobalPoint(0,0,0))
	     << endl;
      }
    }
    PixelRecoRange<float> phiRange = deltaPhi(outerHitsMap.x[io], 
					      outerHitsMap.y[io], 
					      outerHitsMap.z[io], 
					      nSigmaPhi*outerHitsMap.drphi[io]
					      );    


    if (debug) {
      unsigned int theIdOut = ohit->rawId();
      if (idOut==theIdOut) {
	cout << "phiRange=" << phiRange.first << "," << phiRange.second << endl;
      }
    }
    if (phiRange.empty()) continue;

    const HitRZCompatibility *checkRZ = region.checkRZ(theInnerLayer.detLayer(), ohit, iSetup,theOuterLayer.detLayer(), 
						       outerHitsMap.rv(io),outerHitsMap.z[io],
						       outerHitsMap.isBarrel ? outerHitsMap.du[io] :  outerHitsMap.dv[io],
						       outerHitsMap.isBarrel ? outerHitsMap.dv[io] :  outerHitsMap.du[io]
						       );
    if(!checkRZ) {
      if (debug) {
	unsigned int theIdOut = ohit->rawId();
	if (idOut==theIdOut) {
	  cout << "did not pass checkRZ" << endl;
	}
      }
      continue;
    }

    Kernels<HitZCheck,HitRCheck,HitEtaCheck> kernels;

    auto innerRange = innerHitsMap.doubleRange(phiRange.min(), phiRange.max());
    LogDebug("HitPairGeneratorFromLayerPair")<<
      "preparing for combination of: "<< innerRange[1]-innerRange[0]+innerRange[3]-innerRange[2]
				      <<" inner and: "<< outerHitsMap.theHits.size()<<" outter";
    for(int j=0; j<3; j+=2) {
      auto b = innerRange[j]; auto e=innerRange[j+1];
      bool ok[e-b];
      switch (checkRZ->algo()) {
	case (HitRZCompatibility::zAlgo) :
	  std::get<0>(kernels).set(checkRZ);
	  std::get<0>(kernels)(b,e,innerHitsMap, ok);
	  break;
	case (HitRZCompatibility::rAlgo) :
	  std::get<1>(kernels).set(checkRZ);
	  std::get<1>(kernels)(b,e,innerHitsMap, ok);
	  break;
	case (HitRZCompatibility::etaAlgo) :
	  std::get<2>(kernels).set(checkRZ);
	  std::get<2>(kernels)(b,e,innerHitsMap, ok);
	  break;
      }
      for (int i=0; i!=e-b; ++i) {
	if (!ok[i]) continue;
	if (debug) {
	  unsigned int theIdOut = ohit->rawId();
	  Hit const & ihit =  innerHitsMap.theHits[b+i].hit();
	  unsigned int theIdIn = ihit->rawId();
	  if (idOut==theIdOut&&idIn==theIdIn) {
	    cout << "found inner hit with id=" << idOut << " and position=" << ihit->globalPosition() 
		 << " phi=" << ihit->globalPosition().phi() 
		 << " z=" << ihit->globalPosition().z() 
		 << " r=" << ihit->globalPosition().perp() 
		 << " trans: " << (ihit->transientHits().size()>1 ? ihit->transientHits()[0]->globalPosition() : GlobalPoint(0,0,0)) << " "
		 << (ihit->transientHits().size()>1 ? ihit->transientHits()[1]->globalPosition() : GlobalPoint(0,0,0))
		 << endl;
	  }
	}
	if (theMaxElement!=0 && result.size() >= theMaxElement){
	  result.clear();
	  edm::LogError("TooManyPairs")<<"number of pairs exceed maximum, no pairs produced";
	  delete checkRZ;
	  return result;
	}
        result.add(b+i,io);
      }
    }
    delete checkRZ;
  }
  LogDebug("HitPairGeneratorFromLayerPair")<<" total number of pairs provided back: "<<result.size();
  return result;
}


