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
  FevEffectRecord e;e.name="FMOD Distortion";e.points={{{0,0x3e800000,1}},{{0x3f800000,0x3f000000,2}}};
  event.layer_records[0].effects.push_back(e);
  assert(ValidFevCurve(e)&&SampleFevCurveExact(e,0)==.25f&&SampleFevCurveExact(e,1)==.5f&&!SampleFevCurveExact(e,.5f));
  std::array<FevDspBinding,1> bindings{{{0,0,0,1,1}}};
  std::array<float,1> a{1},b{3};
  std::array<FevParameterSnapshot,2> snapshots{{{0,a},{2,b}}};
  const auto blocks=BuildFevNamedDspBlocks(event,0,bindings,snapshots,48000);
  assert(blocks&&blocks->size()==2&&(*blocks)[0].chain.stages[0].value==.25f&&(*blocks)[1].chain.stages[0].value==.5f);
  std::vector<int16_t> pcm(8,8192);assert(ApplyScheduledDsp(pcm,48000,2,*blocks));
  assert(pcm[0]!=8192&&pcm[0]!=pcm[4]&&pcm[4]==16384);
  b[0]=2;assert(!BuildFevNamedDspBlocks(event,0,bindings,snapshots,48000));
  event.layer_records[0].effects[0].points[1][1]=0x3e800000;
  assert(BuildFevNamedDspBlocks(event,0,bindings,snapshots,48000));
  if(argc==1)return 0;
  assert(argc==2);std::ifstream file(argv[1],std::ios::binary);assert(file);
  unsigned projects=0,parameters=0,curves=0,constants=0,named=0,named_constants=0,points=0;
  for(;;){uint32_t h[4];if(!file.read(reinterpret_cast<char*>(h),16)){assert(file.eof()&&!file.gcount());break;}
    std::vector<std::byte> bytes(h[3]);assert(file.read(reinterpret_cast<char*>(bytes.data()),bytes.size()));if(!h[0])continue;
    auto t=AudioText(bytes);auto end=t.find('\0');auto table=InspectFevEventTable(AudioBytes(bytes).subspan(end+1),t.substr(0,end));assert(table);++projects;
    for(const auto& ev:table->events){for(const auto& param:ev.parameters){++parameters;auto range=ReadFevParameterDomain(param);assert(range);assert(range->Normalize(range->minimum)==0.f&&range->Normalize(range->maximum)==1.f);assert(range->Physical(0.f)==range->minimum&&range->Physical(1.f)==range->maximum);}
      for(const auto& layer:ev.layer_records)for(const auto& fx:layer.effects){++curves;assert(ValidFevCurve(fx));bool constant=true;auto initial=ReadFevCurvePoint(fx.points.front()).value;
        for(const auto& raw:fx.points){++points;const auto point=ReadFevCurvePoint(raw);assert(SampleFevCurveExact(fx,point.position)==point.value);constant &= point.value==initial;}
        if(constant){++constants;assert(SampleFevCurveExact(fx,.37f)==initial);}if(!fx.name.empty()){++named;assert(ClassifyFevDsp(fx.name)!=DspKind::Unknown);named_constants+=constant;}
      }
    }
  }
  std::cout<<projects<<" projects, "<<parameters<<" parameter domains, "<<curves<<" curves, "<<points<<" points, "<<constants<<" constant curves, "<<named<<" named, "<<named_constants<<" named constant\n";
}
