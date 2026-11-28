#include "../native_port/src/eor_fev_automation.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <limits>
using namespace hulk::eor::audio;
int main(int argc,char** argv){
  const FevParameterDomain domain{1,3};
  assert(domain.Normalize(0)==0.f&&domain.Normalize(2)==.5f&&domain.Normalize(4)==1.f);
  assert(domain.Physical(.5f)==2.f&&!domain.Physical(-1));
  assert(!domain.Normalize(std::numeric_limits<float>::quiet_NaN()));
  assert((!FevParameterDomain{1,1}.Normalize(1)));
  std::array<std::byte,24> fields{};fields[7]=std::byte{0x3f};fields[6]=std::byte{0x80};fields[11]=std::byte{0x40};fields[10]=std::byte{0x40};
  FevEventEntry event;event.parameters.push_back({"test",fields,{}});event.layer_records.resize(1);
  FevEffectRecord e;e.control=UINT32_MAX;e.header[1]=4;e.name="FMOD Distortion";e.points={{{0,0x3e800000,1}},{{0x3f800000,0x3f000000,2}}};
  event.layer_records[0].effects.push_back(e);
  assert(ValidFevCurve(e)&&SampleFevCurve(e,0)==.25f&&SampleFevCurve(e,1)==.5f&&SampleFevCurve(e,.5f)==.375f);
  std::array<FevDspBinding,1> bindings{{{0,0,0,1,1}}};
  std::array<float,1> a{1},b{3};
  std::array<FevParameterSnapshot,2> snapshots{{{0,a},{2,b}}};
  const auto blocks=BuildFevNamedDspBlocks(event,0,bindings,snapshots,48000);
  assert(blocks&&blocks->size()==2&&(*blocks)[0].chain.stages[0].value==.25f&&(*blocks)[1].chain.stages[0].value==.5f);
  std::vector<int16_t> pcm(8,8192);assert(ApplyScheduledDsp(pcm,48000,2,*blocks));
  assert(pcm[0]!=8192&&pcm[0]!=pcm[4]&&pcm[4]==16384);
  const auto automatic=BuildFevNamedDspBlocks(event,0,{},snapshots,48000);
  assert(automatic&&(*automatic)[0].chain.stages[0].value==.25f&&(*automatic)[1].chain.stages[0].value==.5f);
  FevEffectRecord volume=e;volume.name={};volume.header[1]=0x0c;
  event.layer_records[0].effects.push_back(volume);
  auto controls=EvaluateFevLayerGainPitch(event,0,b);
  assert(controls&&controls->values.gain==.5f&&controls->values.ratio==1.f&&!controls->unhandled);
  event.layer_records[0].effects.back().header[1]=0x14;
  controls=EvaluateFevLayerGainPitch(event,0,a);
  assert(controls&&controls->values.ratio==.25f);
  event.layer_records[0].effects.back().header[1]=0x24;
  controls=EvaluateFevLayerGainPitch(event,0,b);assert(controls&&controls->unhandled==1);
  assert(!ResolveFevFixedGainPitch(event,0,snapshots));
  event.layer_records[0].effects.back().header[1]=0x0c;
  assert(!ResolveFevFixedGainPitch(event,0,snapshots));
  event.layer_records[0].effects.back().points[1][1]=0x3e800000;
  const auto fixed=ResolveFevFixedGainPitch(event,0,snapshots);
  assert(fixed&&fixed->gain==.25f&&fixed->ratio==1.f);
  event.layer_records[0].effects.pop_back();
  b[0]=2;const auto mid=BuildFevNamedDspBlocks(event,0,bindings,snapshots,48000);
  assert(mid&&(*mid)[1].chain.stages[0].value==.375f);
  event.layer_records[0].effects[0].points[1][1]=0x3e800000;
  assert(BuildFevNamedDspBlocks(event,0,bindings,snapshots,48000));
  // Shape is taken from the RIGHT knot, not the left. Exercise both directions.
  for(uint32_t shape:{1u,2u,4u,8u}){
    FevEffectRecord curve;
    curve.points={{{0,0,2}},{{0x3f800000,0x3f800000,shape}}};
    const float expected=shape==1?.15625f:shape==2?.25f:shape==4?float((std::pow(1025.,.25)-1)/1024.):.4375f;
    const auto v=SampleFevCurve(curve,.25f);assert(v&&std::abs(*v-expected)<2e-6f);
    curve.points[0][1]=0x3f800000;curve.points[1][1]=0;
    const auto descending=SampleFevCurve(curve,.75f);assert(descending&&std::abs(*descending-expected)<2e-6f);
  }
  FevEffectRecord tail;tail.points={{{0,0,2}},{{0x3f000000,0x3f800000,2}}};
  assert(SampleFevCurve(tail,.75f)==1.f);
  tail.points[0][0]=0x3e800000;assert(!SampleFevCurve(tail,.1f));
  FevEffectRecord cutoff;cutoff.name="FMOD Lowpass";cutoff.tail[1]=1;
  assert(MapOriginalNamedDspValue(cutoff,0)==10.f&&MapOriginalNamedDspValue(cutoff,1)==22000.f);
  const auto log_mid=MapOriginalNamedDspValue(cutoff,.5f);
  assert(log_mid&&std::abs(*log_mid-std::sqrt(220000.f))<.001f);
  cutoff.tail[1]=0;assert(MapOriginalNamedDspValue(cutoff,.5f)==11005.f);
  cutoff.tail[1]=2;assert(!MapOriginalNamedDspValue(cutoff,.5f));
  cutoff.tail[1]=1;cutoff.header[0]=1;assert(!MapOriginalNamedDspValue(cutoff,.5f));
  if(argc==1)return 0;
  assert(argc==2);std::ifstream file(argv[1],std::ios::binary);assert(file);
  unsigned projects=0,parameters=0,curves=0,constants=0,named=0,named_constants=0,points=0,interpolated=0,mapped=0;
  for(;;){uint32_t h[4];if(!file.read(reinterpret_cast<char*>(h),16)){assert(file.eof()&&!file.gcount());break;}
    std::vector<std::byte> bytes(h[3]);assert(file.read(reinterpret_cast<char*>(bytes.data()),bytes.size()));if(!h[0])continue;
    auto t=AudioText(bytes);auto end=t.find('\0');auto table=InspectFevEventTable(AudioBytes(bytes).subspan(end+1),t.substr(0,end));assert(table);++projects;
    for(const auto& ev:table->events){for(const auto& param:ev.parameters){++parameters;auto range=ReadFevParameterDomain(param);assert(range);assert(range->Normalize(range->minimum)==0.f&&range->Normalize(range->maximum)==1.f);assert(range->Physical(0.f)==range->minimum&&range->Physical(1.f)==range->maximum);}
      std::vector<float> values;
      for(const auto& param:ev.parameters)values.push_back(ReadFevParameterDomain(param)->minimum);
      for(uint32_t layer=0;layer<ev.layer_records.size();++layer){
        const auto routes=ResolveOriginalNamedDspBindings(ev,layer);assert(routes);
        for(const auto& binding:*routes){assert(binding.parameter==ev.layer_records[layer].effects[binding.effect].tail[0]);}
        assert(EvaluateFevLayerGainPitch(ev,layer,values));
      }
      for(const auto& layer:ev.layer_records)for(const auto& fx:layer.effects){++curves;assert(ValidFevCurve(fx));bool constant=true;auto initial=ReadFevCurvePoint(fx.points.front()).value;
        for(const auto& raw:fx.points){++points;const auto point=ReadFevCurvePoint(raw);assert(SampleFevCurve(fx,point.position)==point.value);constant &= point.value==initial;}
        for(unsigned i=0;i<=256;++i){const auto sample=SampleFevCurve(fx,float(i)/256.f);assert(sample&&std::isfinite(*sample)&&*sample>=0&&*sample<=1);++interpolated;
          if(!fx.name.empty()){const auto physical=MapOriginalNamedDspValue(fx,*sample);assert(physical&&std::isfinite(*physical));const auto range=OriginalUsaDspRange(ClassifyFevDsp(fx.name),0);assert(range&&*physical>=range->minimum&&*physical<=range->maximum);++mapped;}}
        if(constant){++constants;assert(SampleFevCurve(fx,.37f)==initial);}if(!fx.name.empty()){++named;assert(ClassifyFevDsp(fx.name)!=DspKind::Unknown);named_constants+=constant;}
      }
    }
  }
  std::cout<<projects<<" projects, "<<parameters<<" parameter domains, "<<curves<<" curves, "<<points<<" points, "<<constants<<" constant curves, "<<named<<" named, "<<named_constants<<" named constant, "<<interpolated<<" evaluated positions, "<<mapped<<" mapped DSP values\n";
}
