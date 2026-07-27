#include "digitor/rgb_curves.hpp"
#include <cassert>
#include <cmath>
#include <limits>
#include <vector>

void test_rgb_curves(){using namespace digitor;
 RgbCurvesParameters p;auto identity=CompiledRgbCurves::compile(p);Color a{-.25f,1.5f,.375f,.37f};auto b=identity->apply(a);assert(b.r==a.r&&b.g==a.g&&b.b==a.b&&b.a==a.a);
 p.red.points={{0,0},{.25f,.1f},{.5f,.8f},{1,1}};auto curve=CompiledRgbCurves::compile(p);auto hit=CompiledRgbCurves::compile(p);assert(curve==hit&&curve->lut_size()==1024&&curve->identity()==curve->serialize());b=curve->apply(a);assert(b.g==a.g&&b.b==a.b&&b.a==a.a);
 p.lut_size=256;assert(CompiledRgbCurves::compile(p)!=curve);p.lut_size=4096;assert(CompiledRgbCurves::compile(p)->curves()[1].samples.size()==4096);
 RgbCurvesParameters flat;flat.master.points={{0,0},{.3f,.2f},{.7f,.2f},{1,1}};auto f=CompiledRgbCurves::compile(flat);float last=-1;for(int n=0;n<=100;n++){float v=f->apply({n/100.f,0,0,1}).r;assert(v+1e-6f>=last);last=v;}
 RgbCurvesParameters ext;ext.master.points={{0,.1f},{1,.9f}};auto e=CompiledRgbCurves::compile(ext);b=e->apply({-.5f,1.5f,.5f,.2f});assert(b.r<.1f&&b.g>.9f&&b.a==.2f);
 bool bad=false;try{RgbCurvesParameters q;q.red.points={{0,0},{0,1}};(void)CompiledRgbCurves::compile(q);}catch(const std::invalid_argument&){bad=true;}assert(bad);
 bad=false;try{RgbCurvesParameters q;q.blue.points[0].x=std::numeric_limits<float>::quiet_NaN();(void)CompiledRgbCurves::compile(q);}catch(const std::invalid_argument&){bad=true;}assert(bad);
 bad=false;try{RgbCurvesParameters q;q.master.domain_min=2;(void)CompiledRgbCurves::compile(q);}catch(const std::invalid_argument&){bad=true;}assert(bad);
 std::vector<Color> in(15,a),out(15);curve->apply(in,out);for(auto q:out)assert(q.a==a.a);
}
