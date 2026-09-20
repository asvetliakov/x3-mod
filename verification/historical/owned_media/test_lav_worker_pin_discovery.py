"""Failure injection into the exact production unique_pin method, using COM fakes."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
PREFIX=r'''
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <iostream>
using HRESULT=std::int32_t;using ULONG=unsigned long;using PIN_DIRECTION=int;
constexpr HRESULT S_OK=0,S_FALSE=1,E_FAIL=-1,E_UNEXPECTED=-2;
constexpr int PINDIR_INPUT=0,PINDIR_OUTPUT=1,MEDIATYPE_Video=1;
static bool FAILED(HRESULT value){return value<0;}
static unsigned allocations=0,checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<__LINE__<<": "<<#x<<"\n";return 1;}}while(0)
struct AM_MEDIA_TYPE {int majortype=1,subtype=2,formattype=3;ULONG cbFormat=0;};
void CoTaskMemFree(void* p){if(p){--allocations;std::free(p);}}
enum class MetadataKind{type};struct Metadata{MetadataKind kind{};int guid[3]{};long long a=0;};
void copy_guid(int& a,int b){a=b;}
struct Owner{void meta(const Metadata&) {}};
struct IEnumMediaTypes{
 std::vector<HRESULT> outcomes{S_OK,S_FALSE};unsigned position=0,releases=0;
 HRESULT Next(unsigned,AM_MEDIA_TYPE** out,ULONG* fetched){*out=nullptr;*fetched=0;HRESULT hr=position<outcomes.size()?outcomes[position++]:S_FALSE;
  if(hr==S_OK){*out=static_cast<AM_MEDIA_TYPE*>(std::calloc(1,sizeof(AM_MEDIA_TYPE)));++allocations;(*out)->majortype=MEDIATYPE_Video;*fetched=1;}return hr;}
 void Release(){++releases;}
};
struct IPin{
 HRESULT direction_hr=S_OK,types_hr=S_OK;int direction=PINDIR_OUTPUT;unsigned releases=0;IEnumMediaTypes types;
 HRESULT QueryDirection(PIN_DIRECTION* value){*value=direction;return direction_hr;}
 HRESULT EnumMediaTypes(IEnumMediaTypes** value){*value=types_hr==S_OK?&types:nullptr;return types_hr;}
 void Release(){++releases;}
};
struct IEnumPins{
 std::vector<IPin*> values;unsigned position=0,releases=0;HRESULT final_hr=S_FALSE;
 HRESULT Next(unsigned,IPin** value,ULONG* fetched){*value=nullptr;*fetched=0;if(position==values.size())return final_hr;*value=values[position++];*fetched=1;return S_OK;}
 void Release(){++releases;}
};
struct IBaseFilter{IEnumPins pins;HRESULT EnumPins(IEnumPins** value){*value=&pins;return S_OK;}};
struct Graph {
 Owner owner;HRESULT failure=S_OK;
 template<class F> HRESULT call(const char*,const char*,F function){return function();}
 bool need(const char*,HRESULT hr){if(hr<0)failure=hr;return hr>=0;}
 template<class T> void release(const char*,T*& value){if(value){value->Release();value=nullptr;}}
 static void free_type(AM_MEDIA_TYPE&){}
'''
SUFFIX=r'''
};
int main(){
 {Graph g;IPin good;IBaseFilter f;f.pins.values={&good};IPin* result=nullptr;
  CHECK(g.unique_pin(&f,PINDIR_OUTPUT,true,&result));CHECK(result==&good);CHECK(good.types.releases==1);CHECK(f.pins.releases==1);CHECK(!allocations);}
 {Graph g;IPin good,bad;bad.direction_hr=E_FAIL;IBaseFilter f;f.pins.values={&good,&bad};IPin* result=nullptr;
  CHECK(!g.unique_pin(&f,PINDIR_OUTPUT,true,&result));CHECK(g.failure==E_FAIL);CHECK(result==&good);CHECK(bad.releases==1);CHECK(f.pins.releases==1);CHECK(!allocations);}
 {Graph g;IPin good,bad;bad.types_hr=E_FAIL;IBaseFilter f;f.pins.values={&good,&bad};IPin* result=nullptr;
  CHECK(!g.unique_pin(&f,PINDIR_OUTPUT,true,&result));CHECK(g.failure==E_FAIL);CHECK(bad.releases==1);CHECK(bad.types.releases==0);CHECK(f.pins.releases==1);CHECK(!allocations);}
 {Graph g;IPin bad;bad.types.outcomes={S_OK,E_FAIL};IBaseFilter f;f.pins.values={&bad};IPin* result=nullptr;
  CHECK(!g.unique_pin(&f,PINDIR_OUTPUT,true,&result));CHECK(!result);CHECK(g.failure==E_FAIL);CHECK(bad.releases==1);CHECK(bad.types.releases==1);CHECK(f.pins.releases==1);CHECK(!allocations);}
 {Graph g;IPin bad;bad.types.outcomes=std::vector<HRESULT>(128,S_OK);IBaseFilter f;f.pins.values={&bad};IPin* result=nullptr;
  CHECK(!g.unique_pin(&f,PINDIR_OUTPUT,true,&result));CHECK(!result);CHECK(bad.releases==1);CHECK(bad.types.releases==1);CHECK(f.pins.releases==1);CHECK(!allocations);}
 {Graph g;IPin good;IBaseFilter f;f.pins.values={&good};f.pins.final_hr=E_FAIL;IPin* result=nullptr;
  CHECK(!g.unique_pin(&f,PINDIR_OUTPUT,true,&result));CHECK(g.failure==E_FAIL);CHECK(f.pins.releases==1);CHECK(!allocations);}
 std::cout<<checks<<" exact-method checks passed\n";
}
'''

class PinDiscovery(unittest.TestCase):
    def test_failure_cannot_be_reclassified_as_incompatible_pin(self):
        source=(ROOT/'src/media/lav_graph_inc.h').read_text()
        method=source[source.index('    bool unique_pin('):source.index('    bool observe_context(')]
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);cpp=root/'pins.cpp';exe=root/'pins';cpp.write_text(PREFIX+method+SUFFIX)
            subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],check=True,capture_output=True)
            result=subprocess.run([str(exe)],check=True,capture_output=True,text=True)
            self.assertIn('exact-method checks passed',result.stdout)

if __name__=='__main__':unittest.main()
